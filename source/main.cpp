//////////////////////////////////////////////////////////////////////////////
//
//  --- main.cpp ---
//  Created by Brian Summa
//
//////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "SourcePath.h"
#include <omp.h> 


using namespace Angel;

typedef vec4  color4;
typedef vec4  point4;


//Scene variables
enum{_SPHERE, _SQUARE, _BOX, _BOXEASYSPHERE};
int scene = _SPHERE; //Simple sphere, square or cornell box
std::vector < Object * > sceneObjects;
point4 lightPosition;
color4 lightColor;
point4 cameraPosition;
constexpr float dcam = 0.15f; 

//Recursion depth for raytracer
int maxDepth = 8;

void initGL();

namespace GLState {
int window_width, window_height;

bool render_line;

std::vector < GLuint > objectVao;
std::vector < GLuint > objectBuffer;

GLuint vPosition, vNormal, vTexCoord;

GLuint program;

// Model-view and projection matrices uniform location
GLuint  ModelView, ModelViewLight, NormalMatrix, Projection;

//==========Trackball Variables==========
static float curquat[4],lastquat[4];
/* current transformation matrix */
static float curmat[4][4];
mat4 curmat_a;
/* actual operation  */
static int scaling;
static int moving;
static int panning;
/* starting "moving" coordinates */
static int beginx, beginy;
/* ortho */
float ortho_x, ortho_y;
/* current scale factor */
static float scalefactor;

mat4  projection;
mat4 sceneModelView;

color4 light_ambient;
color4 light_diffuse;
color4 light_specular;

};

/* ------------------------------------------------------- */
/* -- PNG receptor class for use with pngdecode library -- */
class rayTraceReceptor : public cmps3120::png_receptor
{
private:
    const unsigned char *buffer;
    unsigned int width;
    unsigned int height;
    int channels;

public:
    rayTraceReceptor(const unsigned char *use_buffer,
                     unsigned int width,
                     unsigned int height,
                     int channels){
        this->buffer = use_buffer;
        this->width = width;
        this->height = height;
        this->channels = channels;
    }
    cmps3120::png_header get_header(){
        cmps3120::png_header header;
        header.width = width;
        header.height = height;
        header.bit_depth = 8;
        switch (channels)
        {
        case 1:
            header.color_type = cmps3120::PNG_GRAYSCALE;break;
        case 2:
            header.color_type = cmps3120::PNG_GRAYSCALE_ALPHA;break;
        case 3:
            header.color_type = cmps3120::PNG_RGB;break;
        default:
            header.color_type = cmps3120::PNG_RGBA;break;
        }
        return header;
    }
    cmps3120::png_pixel get_pixel(unsigned int x, unsigned int y, unsigned int level){
        cmps3120::png_pixel pixel;
        unsigned int idx = y*width+x;
        /* pngdecode wants 16-bit color values */
        pixel.r = buffer[4*idx]*257;
        pixel.g = buffer[4*idx+1]*257;
        pixel.b = buffer[4*idx+2]*257;
        pixel.a = buffer[4*idx+3]*257;
        return pixel;
    }
};

/* -------------------------------------------------------------------------- */
/* ----------------------  Write Image to Disk  ----------------------------- */
bool write_image(const char* filename, const unsigned char *Src,
                 int Width, int Height, int channels){
    cmps3120::png_encoder the_encoder;
    cmps3120::png_error result;
    rayTraceReceptor image(Src,Width,Height,channels);
    the_encoder.set_receptor(&image);
    result = the_encoder.write_file(filename);
    if (result == cmps3120::PNG_DONE) {
        std::cerr << "finished writing " << filename << "." << std::endl;
        std::string msg = "finished writing ";
        msg += filename;
        msg += "\n"; 
        OutputDebugString(msg.c_str());
    }
    else {
        std::cerr << "write to " << filename << " returned error code " << result << "." << std::endl;
    }
    return result==cmps3120::PNG_DONE;
}


/* -------------------------------------------------------------------------- */
/* -------- Given OpenGL matrices find ray in world coordinates of ---------- */
/* -------- window position x,y --------------------------------------------- */
std::vector < vec4 > findRay(GLdouble x, GLdouble y){

    y = GLState::window_height-y;

    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    GLdouble modelViewMatrix[16];
    GLdouble projectionMatrix[16];
    for(unsigned int i=0; i < 4; i++){
        for(unsigned int j=0; j < 4; j++){
            modelViewMatrix[j*4+i]  =  GLState::sceneModelView[i][j];
            projectionMatrix[j*4+i] =  GLState::projection[i][j];
        }
    }


    GLdouble nearPlaneLocation[3];
    _gluUnProject(x, y, 0.0, modelViewMatrix, projectionMatrix,
                  viewport, &nearPlaneLocation[0], &nearPlaneLocation[1],
            &nearPlaneLocation[2]);

    GLdouble farPlaneLocation[3];
    _gluUnProject(x, y, 1.0, modelViewMatrix, projectionMatrix,
                  viewport, &farPlaneLocation[0], &farPlaneLocation[1],
            &farPlaneLocation[2]);


    vec4 ray_origin = vec4(nearPlaneLocation[0], nearPlaneLocation[1], nearPlaneLocation[2], 1.0);
    vec3 temp = vec3(farPlaneLocation[0]-nearPlaneLocation[0],
            farPlaneLocation[1]-nearPlaneLocation[1],
            farPlaneLocation[2]-nearPlaneLocation[2]);
    temp = normalize(temp);
    vec4 ray_dir = vec4(temp.x, temp.y, temp.z, 0.0);

    std::vector < vec4 > result(2);
    result[0] = ray_origin;
    result[1] = ray_dir;

    return result;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
bool intersectionSort(Object::IntersectionValues i, Object::IntersectionValues j){
    return (i.t < j.t);
}

/* -------------------------------------------------------------------------- */
/* ---------  Some debugging code: cast Ray = p0 + t*dir  ------------------- */
/* ---------  and print out what it hits =                ------------------- */
void castRayDebug(vec4 p0, vec4 dir){

    std::vector < Object::IntersectionValues > intersections;
    Object::IntersectionValues closest;
    closest.ID_ = -1; 

    for(unsigned int i=0; i < sceneObjects.size(); i++){
        intersections.push_back(sceneObjects[i]->intersect(p0, dir));
        intersections[intersections.size()-1].ID_ = i;
    }

    closest.t = std::numeric_limits< double >::infinity(); 

    for(unsigned int i=0; i < intersections.size(); i++){
        if(intersections[i].t != std::numeric_limits< double >::infinity()){
            
            vec4 L = lightPosition-intersections[i].P;
            L  = normalize(L);

            std::string message = "Hit " + intersections[i].name + " " + std::to_string(intersections[i].ID_) + "\n";
            message += "P: " + intersections[i].P.to_string() + "\n";
            message += "N: " + intersections[i].N.to_string() + "\n";
            message += "L: " + L.to_string() + "\n";
            message += "t: " + std::to_string(intersections[i].t) + "\n";
            message += "Name : " + intersections[i].name + "\n"; 
            OutputDebugString(message.c_str()); 

            if (std::fabs(intersections[i].t) < closest.t && intersections[i].t > 0.0)
            {
                closest.t = intersections[i].t; 
                closest.name = intersections[i].name;
                closest.ID_ = intersections[i].ID_; 
            }
   
            OutputDebugString("---------------------\n");
        }

        
    }

    OutputDebugString("~~~~~~~~~~~~~~\n");

    if (closest.ID_ != -1) {
        std::string message = "Closest " + closest.name + "\n";
        OutputDebugString(message.c_str());
    }

    OutputDebugString("=============================\n");
    

}

/* -------------------------------------------------------------------------- */

// utility function 
void clampColor(vec4& color) {
    color.x = min(1.0, color.x); 
    color.y = min(1.0, color.y);
    color.z = min(1.0, color.z);
    color.w = 1.0;
}

void equalizeColor(vec4& color) 
{
    double colorMax = max(max(color.x, color.y), color.z);

    if (colorMax > 1.0)
    {
        color.x /= colorMax;
        color.y /= colorMax;
        color.z /= colorMax;
    }

    color.w = 1.0;
}



// shadow Feeler : true if hits any object before reaching lightsource 
bool shadowFeeler(const vec4& p0, Object *object, const vec4& lightp){
    bool inShadow = false;

    // Light Direction
    vec4 L = lightp - p0;
    Angel::normalize(L);
    L.w = 0.0;

    // Cast a single ray towards Light 
    // -------------------------------
    Object::IntersectionValues rayXover;
    rayXover.t = std::numeric_limits< double >::infinity();

    // Test if intersect with scene objects and if closest than light 
    // Cast a Ray from p0, direction L
    // if intersection 
    for (unsigned int i = 0; i < sceneObjects.size(); i++) {
        // transparent material doesn't cast shadow 
        if (sceneObjects[i]->shadingValues.Kt > 0.8) {
            continue;
        }

        //  Shadow Acne : move towards light
        Object::IntersectionValues inter = sceneObjects[i]->intersect(p0, L);
        if (std::abs(inter.t) < rayXover.t && inter.t > EPSILON) {
            rayXover = inter;
            rayXover.ID_ = i; 
        }
    }

    if (rayXover.ID_ != -1)
    {
        // test if object is before light 
        double lp0 = Angel::length(lightp - p0);
        double lpX = Angel::length(rayXover.P - p0);
        inShadow = (lp0 - lpX ) > 0.0;
    }
    return inShadow;
}

// Soft Shadows from an area lightsource (uniform sampling of source in square 2.0 x 2.0)
// advise : Nsamples = 128 or 256 to get interesting render
float softShadow(const vec4& p0, Object* object, const int& Nsamples=10)
{
    // Soft Shadows 
    int nInShadows = 0;
    double squareSide = 5.0;
    std::vector<vec4> lightspos = { lightPosition };

    // Take N samples 
    for (int k = 0; k < Nsamples-1; k++) {

        double x = (-squareSide / 2.0) + (std::rand()) / ((double)RAND_MAX / squareSide);
        double z = (-squareSide / 2.0) + (std::rand()) / ((double)RAND_MAX / squareSide);

        lightspos.push_back(lightPosition + vec4(x, 0.0, z, 0.0));
    }

    //#pragma omp parallel for shared(nInShadows)
    for (int klight = 0; klight < Nsamples; klight++)
    {
        bool inshadowK = shadowFeeler(p0, object, lightspos[klight]);
        if (inshadowK) { nInShadows++;  }

    }

    return (float)nInShadows / (float)Nsamples;
}

double schlick(const double& cosT, const double& nrf)
{
    double r0 = (1.0 - nrf) / (1.0 + nrf); 
    r0 *= r0; 
    return r0 + (1.0 - r0) * std::pow((1.0 - cosT), 5); 
}


/* -------------------------------------------------------------------------- */
/* ----------  cast Ray = p0 + t*dir and intersect with sphere      --------- */
/* ----------  return color, right now shading is approx based      --------- */
/* ----------  depth                                                --------- */
vec4 castRay(vec4 p0, vec4 E, Object *lastHitObject, int depth){
    vec4 color = vec4(0.0,0.0,0.0,0.0);

    if(depth > maxDepth){ return color; }

    //TODO: Raytracing code here

    std::vector < Object::IntersectionValues > intersections;

    for (unsigned int i = 0; i < sceneObjects.size(); i++) {
        intersections.push_back(sceneObjects[i]->intersect(p0, E));
        intersections[intersections.size() - 1].ID_ = i;
    }

    Object::IntersectionValues closest; 
    closest.t = std::numeric_limits< double >::infinity(); 
    closest.ID_ = -1; 

    for (unsigned int i = 0; i < intersections.size(); i++) {
        if (intersections[i].t != std::numeric_limits< double >::infinity()) {
            
            if (std::fabs(intersections[i].t) < closest.t && intersections[i].t > 2.0 * EPSILON){
                closest = intersections[i]; 
            }

        }
    }

    if (closest.ID_ == -1) {
        return color; 
    }

    
        
    // Ambiant Ia = Isa * Ka 
    // ----------------------
    double Isa = 1.0;
    double ambiant = Isa * sceneObjects[closest.ID_]->shadingValues.Ka;

    // Light Position 
    // ---------------
    vec4 L = lightPosition - closest.P;
    L = normalize(L);
    L.w = 0.0; 

    // Diffuse
    // --------
    double Id = 1.0; 
    double diffuse = Id * sceneObjects[closest.ID_]->shadingValues.Kd * max(Angel::dot(L, closest.N), 0.0); 

    // Direction Vector : R , V 
    // ---------------------------
    // R : reflected direction 
    // V : towards camera
    vec4 R = -reflect(L, closest.N); // =  2.0 * Angel::dot(closest.N, L) * closest.N - L;
    R = Angel::normalize(R);
    R.w = 0.0; 

    vec4 V = -E; // - direction du rayon 
    V.w = 0.0;

    // Specular :  Is = Iss * Ks * dot(R, V)^n 
    // ----------------------------------------
    double Iss = 1.0;
    double nr = sceneObjects[closest.ID_]->shadingValues.Kn; // exponent
    double specular = Iss * sceneObjects[closest.ID_]->shadingValues.Ks * std::pow(max(Angel::dot(V, R), 0.0), nr);
        

    // ===============
    // Phong Equation
    // ===============
    color = (ambiant * lightColor + diffuse * lightColor )* sceneObjects[closest.ID_]->shadingValues.color;
    color += specular * lightColor * sceneObjects[closest.ID_]->shadingValues.color;
    equalizeColor(color);

    // ==========================================
    // Shadows :
    // ----------
    // Compute "hard" shadow if Nsamples = 1 
    // Compute soft Shadows if Nsamples > 1 ( require at least 128 or 256 shadow rays) 
    
    int nsamples = 256;
    float percentageShadowed = softShadow(closest.P + L * EPSILON, sceneObjects[closest.ID_], nsamples); // 0 : 1
    // Applied Shadows
    color *= (1.0 - percentageShadowed);
    color.w = 1.0;
    
    // ==========================================
    // Recursivity Rays
    // ==================
    double attenuation = 1.0 / (double)(depth + 1.0);

    // Transparency  
    // ------------
    // if last material is transparent add color of hitten object 
    color4 refractColor = vec4(0.0, 0.0, 0.0, 0.0);
    if (sceneObjects[closest.ID_]->shadingValues.Kt > 0.0 && sceneObjects[closest.ID_]->shadingValues.Kr > 0.0)
    {
        // Refraction 
        // ----------
        // kr1 * sin(theta1) = kr2 * sin(theta2) 
            
        // Air coefficient of refraction 
        double kr1 = 1.0; 
        /*if (lastHitObject != nullptr && lastHitObject->shadingValues.Kr > 0.0) {
            kr1 = lastHitObject->shadingValues.Kr; 
        }*/

        double kr2 = sceneObjects[closest.ID_]->shadingValues.Kr; 
            
        vec4 vecInc = E; 
        vecInc = normalize(vecInc);
        vecInc.w = 0.0;
        // transmission : vector of refracted ray 
        double cosTheta = Angel::dot(vecInc, normalize(closest.N)); // out 
        vec4 normalPlanR = closest.N;
        // outside towards air 
        if (cosTheta > 0.0)
        {
            normalPlanR = -closest.N;
            kr2 = kr1; 
            kr1 = sceneObjects[closest.ID_]->shadingValues.Kr;  
            cosTheta = -cosTheta; 
        }

        double nrf = kr1 / kr2; // n = n1 / n2 
        double cosTheta2 = dot(vecInc, normalPlanR); 

        double discriminant = 1.0 - (nrf * nrf) * (1.0 - cosTheta2*cosTheta2);

        double reflect_prob = discriminant > 0.0 ? schlick(cosTheta, nrf) : 1.0; // reflection si refraction impossible
        double randN = std::rand() / (RAND_MAX + 1.0); 

        if (discriminant < 0.0)
        {
            // refraction => reflection
            vec4 dirReflected = -reflect(V, closest.N);
            //refractColor = vec4(0.8, 0.2, 0.2, 1.0); 
            refractColor = castRay(closest.P, dirReflected, sceneObjects[closest.ID_], depth + 1);
            refractColor = refractColor * lightColor;
            clampColor(refractColor);
        }
        else
        {
            // Refraction 
            vec4 dirRefractTan = nrf * (vecInc - dot(vecInc, normalPlanR) * normalPlanR); // composante tangentielle 
            vec4 dirRefractNor = - normalPlanR * std::sqrt(discriminant); // composante normale
            vec4 dirRefract = dirRefractTan + dirRefractNor;
            dirRefract = normalize(dirRefract);
            dirRefract.w = 0.0;
            refractColor = castRay(closest.P - dirRefract * EPSILON, dirRefract, sceneObjects[closest.ID_], depth + 1);
            equalizeColor(refractColor);
        }

    }

    // Specular Contribution secondary rays 
    //-----------------------------------------

    color4 specColor = vec4(0.0, 0.0, 0.0, 0.0);
    if (sceneObjects[closest.ID_]->shadingValues.Ks > 0.0)
    {
        vec4 reflectionDir = -reflect(V, closest.N);
        specColor = castRay(closest.P, reflectionDir, sceneObjects[closest.ID_], depth + 1);
        equalizeColor(specColor);
    }
    /*else if (lastHitObject != nullptr && sceneObjects[closest.ID_]->shadingValues.Kt == 0.0)
    {
        equalizeColor(color);
        return color;
    }*/

    color = sceneObjects[closest.ID_]->shadingValues.Kt * refractColor +
            sceneObjects[closest.ID_]->shadingValues.Ks * specColor * attenuation +
            color * max(0.0, (1.0 - 
                                sceneObjects[closest.ID_]->shadingValues.Ks * attenuation -
                                sceneObjects[closest.ID_]->shadingValues.Kt));

    equalizeColor(color);

    return color;
}


/* -------------------------------------------------------------------------- */
/* ------------  Ray trace our scene.  Output color to image and    --------- */
/* -----------   Output color to image and save to disk             --------- */
void rayTrace(){

    static unsigned int nraysample = 64; 
    unsigned char *buffer = new unsigned char[GLState::window_width*GLState::window_height*4];


    for(int i=0; i < GLState::window_width; i++){
        
        for(int j=0; j < GLState::window_height; j++){

            int idx = j*GLState::window_width+i;
            /*std::vector < vec4 > ray_o_dir = findRay(i, j);
            vec4 color = castRay(ray_o_dir[0], vec4(ray_o_dir[1].x, ray_o_dir[1].y, ray_o_dir[1].z, 0.0), NULL, 0);*/

            // anti aliasing 
            vec4 color(0.0, 0.0, 0.0, 0.0);
            double cx = 0.0;  
            double cy = 0.0;  
            double cz = 0.0;  
            //#pragma omp parallel for reduction(+:cx,cy,cz,cw)
            for (int k = 0; k < nraysample; k++) {
                double xi = std::rand() / (double)RAND_MAX;
                double yj = std::rand() / (double)RAND_MAX;
                std::vector < vec4 > ray_o_dir = findRay(i +xi, j+ yj);
                vec4 col = castRay(ray_o_dir[0], vec4(ray_o_dir[1].x, ray_o_dir[1].y, ray_o_dir[1].z, 0.0), NULL, 1);
                cx += col.x; 
                cy += col.y; 
                cz += col.z; 
            }

            color = vec4(cx, cy, cz, 0.0) / (double)nraysample;

            // Gamma correction : 2 
            // ---------------------
            // std::pow(color, 1. / gamma  ) 
            color.x = std::sqrt(color.x); 
            color.y = std::sqrt(color.y); 
            color.z = std::sqrt(color.z);
            color.w = 1.0; 
            
            buffer[4*idx]   = color.x*255;
            buffer[4*idx+1] = color.y*255;
            buffer[4*idx+2] = color.z*255;
            buffer[4*idx+3] = color.w*255;
        }
    }

    write_image("output.png", buffer, GLState::window_width, GLState::window_height, 4);

    delete[] buffer;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
void initCornellBox(){
    cameraPosition = point4( 0.0, 0.0, 6.0, 1.0 );
    lightPosition = point4( 0.0, 1.5, 0.0, 1.0 );
    lightColor = color4( 1.0, 1.0, 1.0, 1.0);

    sceneObjects.clear();

    { //Back Wall
        sceneObjects.push_back(new Square("Back Wall", Translate(0.0, 0.0, -2.0)*Scale(2.0,2.0,1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.2,0.8,1.0,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }

    { //Left Wall
        sceneObjects.push_back(new Square("Left Wall", RotateY(90)*Translate(0.0, 0.0, -2.0)*Scale(2.0,2.0,1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0,0.0,0.2,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }

    { //Right Wall
        sceneObjects.push_back(new Square("Right Wall", RotateY(-90)*Translate(0.0, 0.0, -2.0)*Scale(2.0, 2.0, 1.0 )));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.5,0.0,0.5,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }

    { //Floor
        sceneObjects.push_back(new Square("Floor", RotateX(-90)*Translate(0.0, 0.0, -2.0)*Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.3,0.3,0.3,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }

    { //Ceiling
        sceneObjects.push_back(new Square("Ceiling", RotateX(90)*Translate(0.0, 0.0, -2.0)*Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.5,0.5,0.5,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }

    { //Front Wall
        sceneObjects.push_back(new Square("Front Wall",RotateY(180)*Translate(0.0, 0.0, -2.0)*Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0,1.0,1.0,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }


    
    {
        sceneObjects.push_back(new Sphere("Diffuse Yellow Sphere", vec3(1.35, -1.5, -1.80), 0.15));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0, 1.0, 0.0, 1.0);
        _shadingValues.Ka = 0.2;
        _shadingValues.Kd = 0.8;
        _shadingValues.Ks = 0.005;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }
    
    {
        sceneObjects.push_back(new Sphere("Glass sphere", vec3(1.0, -1.25, 0.0),0.75));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0,0.0,0.0,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 0.0;
        _shadingValues.Ks = 0.0; // reflexion speculaire
        _shadingValues.Kn = 16.0;// shininess
        _shadingValues.Kt = 0.8; // coefficient de transmission
        _shadingValues.Kr = 1.4; // indice de refraction
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }
    
    {
        sceneObjects.push_back(new Sphere("Grey Mirrored Sphere", vec3(-1.0, -1.25, -0.5),0.75));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.5,0.5,0.5,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 0.2;
        _shadingValues.Ks = 0.8;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }


    {
        sceneObjects.push_back(new Sphere("Diffuse Green Sphere", vec3(-1.0, -1.75, 0.5), 0.25));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.1, 1.0, 0.1, 1.0);
        _shadingValues.Ka = 0.2;
        _shadingValues.Kd = 0.8;
        _shadingValues.Ks = 0.005;
        _shadingValues.Kn = 32.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }
    
    
    

    /*{
        sceneObjects.push_back(new Sphere("Diffuse Sphere", vec3(1.0, -1.25, 0.5),0.75));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0, 0.0, 0.0, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }*/

    {
        sceneObjects.push_back(new Sphere("Specular + Diffuse Sphere", vec3(-1.15, -1.75, 0.95),0.25));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0,0.8,0.8,1.0);
        _shadingValues.Ka = 0.2;
        _shadingValues.Kd = 0.8;
        _shadingValues.Ks = 0.10;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }
}


void initCornellBox2() {
    cameraPosition = point4(0.0, 0.0, 6.0, 1.0);
    lightPosition = point4(0.0, 1.5, 0.0, 1.0);
    lightColor = color4(1.0, 1.0, 1.0, 1.0);

    sceneObjects.clear();

    { //Back Wall
        sceneObjects.push_back(new Square("Back Wall", Translate(0.0, 0.0, -2.0) * Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.2, 0.8, 1.0, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    { //Left Wall
        sceneObjects.push_back(new Square("Left Wall", RotateY(90) * Translate(0.0, 0.0, -2.0) * Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0, 0.0, 0.2, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    { //Right Wall
        sceneObjects.push_back(new Square("Right Wall", RotateY(-90) * Translate(0.0, 0.0, -2.0) * Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.5, 0.0, 0.5, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    { //Floor
        sceneObjects.push_back(new Square("Floor", RotateX(-90) * Translate(0.0, 0.0, -2.0) * Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.3, 0.3, 0.3, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    { //Ceiling
        sceneObjects.push_back(new Square("Ceiling", RotateX(90) * Translate(0.0, 0.0, -2.0) * Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.5, 0.5, 0.5, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    { //Front Wall
        sceneObjects.push_back(new Square("Front Wall", RotateY(180) * Translate(0.0, 0.0, -2.0) * Scale(2.0, 2.0, 1.0)));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0, 1.0, 1.0, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    // Diffuse 
    for (unsigned int ki = 0; ki < 6; ki++)
    {
        
        {
            vec4 col = vec4(std::rand() / (double)RAND_MAX, std::rand() / (double)RAND_MAX, std::rand() / (double)RAND_MAX, 1.0); 
            double sizeSp = 0.05 + 0.35*std::rand() / (double)RAND_MAX;
            double x = -2.0 + sizeSp + (4.0 - 2.0 * sizeSp) * (std::rand() / (double)(RAND_MAX));
            double z = -1.5 + 2.5 * (std::rand() / (double)(RAND_MAX)); 
            double y = -2.0 + sizeSp + (4.0 - 2.0 - sizeSp) * (std::rand() / (double)(RAND_MAX));
            vec3 spherePos = vec3(x, y , z);
            std::string name = "Amb + Diffuse Sphere " + std::to_string(ki);
            sceneObjects.push_back(new Sphere(name, spherePos, sizeSp));
            Object::ShadingValues _shadingValues;
            _shadingValues.color = col;
            _shadingValues.Ka = 0.2;
            _shadingValues.Kd = 0.8;
            _shadingValues.Ks = 0.0;
            _shadingValues.Kn = 16.0;
            _shadingValues.Kt = 0.0;
            _shadingValues.Kr = 0.0;
            sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
            sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
        }

        {
            vec4 col = vec4(std::rand() / (double)RAND_MAX, std::rand() / (double)RAND_MAX, std::rand() / (double)RAND_MAX, 1.0);
            double sizeSp = 0.05 + 0.35 * std::rand() / (double)RAND_MAX;
            double x = -2.0 + sizeSp + (4.0 - 2.0 * sizeSp) * (std::rand() / (double)(RAND_MAX));
            double z = -1.5 + 2.5 * (std::rand() / (double)(RAND_MAX));
            double y = -2.0 + sizeSp + (4.0 - 2.0 - sizeSp) * (std::rand() / (double)(RAND_MAX));
            vec3 spherePos = vec3(x, y, z);
            std::string name = "Amb + Diffuse + Specular Sphere " + std::to_string(ki);
            sceneObjects.push_back(new Sphere(name, spherePos, sizeSp));
            Object::ShadingValues _shadingValues;
            _shadingValues.color = col;
            _shadingValues.Ka = 0.2;
            _shadingValues.Kd = 0.8;
            _shadingValues.Ks = 0.24;
            _shadingValues.Kn = 16.0;
            _shadingValues.Kt = 0.0;
            _shadingValues.Kr = 0.0;
            sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
            sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
        }
    }

    // Mirrored
    for (unsigned int ki = 0; ki < 3; ki++)
    {

        {
            vec4 col = vec4(std::rand() / (double)RAND_MAX, std::rand() / (double)RAND_MAX, std::rand() / (double)RAND_MAX, 1.0);
            double sizeSp = 0.05 + 0.5 * std::rand() / (double)RAND_MAX;
            double x = -2.0 + sizeSp + (4.0 - 2.0*sizeSp) * (std::rand() / (double)(RAND_MAX));
            double z = -1.5 + 2.5 * (std::rand() / (double)(RAND_MAX));
            double y = -2.0 + sizeSp + (4.0 - 2.0 - sizeSp) * (std::rand() / (double)(RAND_MAX));
            vec3 spherePos = vec3(x, -2.0 + sizeSp, z);
            std::string name = "Mirrored Sphere " + std::to_string(ki); 

            sceneObjects.push_back(new Sphere(name.c_str(), spherePos, sizeSp));
            Object::ShadingValues _shadingValues;
            _shadingValues.color = col;
            _shadingValues.Ka = 0.0;
            _shadingValues.Kd = 0.2;
            _shadingValues.Ks = 0.8;
            _shadingValues.Kn = 16.0;
            _shadingValues.Kt = 0.0;
            _shadingValues.Kr = 0.0;
            sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
            sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());

        }
    }

    // Mini Glass 
    for (unsigned int ki = 0; ki < 3; ki++)
    {

        {
            double sizeSp = 0.05 + 0.35 * std::rand() / (double)RAND_MAX;
            double x = -2.0 + sizeSp + (4.0 - 2.0 * sizeSp) * (std::rand() / (double)(RAND_MAX));
            double z = -1.5 + 2.5 * (std::rand() / (double)(RAND_MAX));
            double y = -2.0 + sizeSp + (4.0 - 2.0 - sizeSp) * (std::rand() / (double)(RAND_MAX));
            vec3 spherePos = vec3(x, y, z);
            std::string name = "Glass Sphere " + std::to_string(ki);

            sceneObjects.push_back(new Sphere(name.c_str(), spherePos, sizeSp));
            Object::ShadingValues _shadingValues;
            _shadingValues.color = vec4(0.9, 0.1, 0.1, 1.0);
            _shadingValues.Ka = 0.0;
            _shadingValues.Kd = 0.0;
            _shadingValues.Ks = 0.0; // reflexion speculaire
            _shadingValues.Kn = 16.0;// shininess
            _shadingValues.Kt = 1.0; // coefficient de transmission
            _shadingValues.Kr = 1.4; // indice de refraction
            sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
            sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());

        }
    }



    /*{
        sceneObjects.push_back(new Sphere("Diffuse Sphere", vec3(1.0, -1.25, -0.5),0.5));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0, 0.0, 0.0, 1.0);
        _shadingValues.Ka = 0.2;
        _shadingValues.Kd = 0.8;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    {
        sceneObjects.push_back(new Sphere("Ambiant + Diffuse Green Sphere", vec3(0.8, 0.8, -0.5 ), 0.15));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.1, 0.8, 0.1, 1.0);
        _shadingValues.Ka = 0.5;
        _shadingValues.Kd = 0.5;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 8.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    {
        sceneObjects.push_back(new Sphere("Silver Sphere", vec3(0.5, 0.0, -1.5), 0.25));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.5, 0.5, 0.5, 1.0);
        _shadingValues.Ka = 0.19225;
        _shadingValues.Kd = 0.50754;
        _shadingValues.Ks = 0.50827;
        _shadingValues.Kn = 32.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    {
        sceneObjects.push_back(new Sphere("Diffuse Yellow Sphere", vec3(0.0, -1.75, -1.5), 0.25));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0, 1.0, 0.0, 1.0);
        _shadingValues.Ka = 0.2;
        _shadingValues.Kd = 0.8;
        _shadingValues.Ks = 0.005;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }

    {
        sceneObjects.push_back(new Sphere("Specular + Ambient Sphere", vec3(-0.5, -1.25, 0.35),0.25));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.2,0.0,1.0,1.0);
        _shadingValues.Ka = 0.2;
        _shadingValues.Kd = 0.8;
        _shadingValues.Ks = 0.2;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }

    {
        sceneObjects.push_back(new Sphere("Grey Mirrored Sphere", vec3(-1.0, -1.25, -0.8), 0.75));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(0.5, 0.5, 0.5, 1.0);
        _shadingValues.Ka = 0.2;
        _shadingValues.Kd = 0.5;
        _shadingValues.Ks = 0.15;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }


    {
        sceneObjects.push_back(new Sphere("Glass sphere", vec3(1.25, -1.25, 0.25), 0.25));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0, 0.0, 0.0, 1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 0.0;
        _shadingValues.Ks = 0.0; // reflexion speculaire
        _shadingValues.Kn = 16.0;// shininess
        _shadingValues.Kt = 1.0; // coefficient de transmission
        _shadingValues.Kr = 1.4; // indice de refraction
        sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
    }*/
}


/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
void initUnitSphere(){
    cameraPosition = point4( 0.0, 0.0, 3.0, 1.0 );
    lightPosition = point4( 0.0, 0.0, 4.0, 1.0 );
    lightColor = color4( 1.0, 1.0, 1.0, 1.0);

    sceneObjects.clear();

    {
        {
            sceneObjects.push_back(new Sphere("Diffuse sphere", vec3(0.5, 0.0, -1.0)));
            Object::ShadingValues _shadingValues;
            _shadingValues.color = vec4(1.0, 0.0, 0.0, 1.0);
            _shadingValues.Ka = 0.0;
            _shadingValues.Kd = 1.0;
            _shadingValues.Ks = 0.0;
            _shadingValues.Kn = 16.0;
            _shadingValues.Kt = 0.0;
            _shadingValues.Kr = 0.0;
            sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
            sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());

        }

        {

            sceneObjects.push_back(new Sphere("Diffuse sphere2", vec3(-1.0, 0.0, 1.0)));
            Object::ShadingValues _shadingValues;
            _shadingValues.color = vec4(0.0, 1.0, 0.0, 1.0);
            _shadingValues.Ka = 0.0;
            _shadingValues.Kd = 1.0;
            _shadingValues.Ks = 0.0;
            _shadingValues.Kn = 16.0;
            _shadingValues.Kt = 0.0;
            _shadingValues.Kr = 0.0;
            sceneObjects[sceneObjects.size() - 1]->setShadingValues(_shadingValues);
            sceneObjects[sceneObjects.size() - 1]->setModelView(mat4());
        }
    }

}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
void initUnitSquare(){
    cameraPosition = point4( 0.0, 0.0, 3.0, 1.0 );
    lightPosition = point4( 0.0, 0.0, 4.0, 1.0 );
    lightColor = color4( 1.0, 1.0, 1.0, 1.0);

    sceneObjects.clear();

    { //Back Wall
        sceneObjects.push_back(new Square("Unit Square"));
        Object::ShadingValues _shadingValues;
        _shadingValues.color = vec4(1.0,0.0,0.0,1.0);
        _shadingValues.Ka = 0.0;
        _shadingValues.Kd = 1.0;
        _shadingValues.Ks = 0.0;
        _shadingValues.Kn = 16.0;
        _shadingValues.Kt = 0.0;
        _shadingValues.Kr = 0.0;
        sceneObjects[sceneObjects.size()-1]->setShadingValues(_shadingValues);
        sceneObjects[sceneObjects.size()-1]->setModelView(mat4());
    }

}


/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_1 && action == GLFW_PRESS){

        if( scene != _SPHERE ){
            initUnitSphere();
            initGL();
            scene = _SPHERE;
        }

    }
    if (key == GLFW_KEY_2 && action == GLFW_PRESS){
        if( scene != _SQUARE ){
            initUnitSquare();
            initGL();
            scene = _SQUARE;
        }
    }
    if (key == GLFW_KEY_3 && action == GLFW_PRESS){
        if( scene != _BOX ){
            initCornellBox();
            initGL();
            scene = _BOX;
        }
    }

    if (key == GLFW_KEY_4 && action == GLFW_PRESS) {
        if (scene != _BOXEASYSPHERE) {
            initCornellBox2();
            initGL();
            scene = _BOXEASYSPHERE;
        }
    }


    if (key == GLFW_KEY_R && action == GLFW_PRESS)
        rayTrace();

    if (key == GLFW_KEY_UP && action == GLFW_PRESS) {
        cameraPosition = Translate(vec3(0.0f, 0.0f, -dcam)) * cameraPosition;
    }
    else if (key == GLFW_KEY_DOWN && action == GLFW_PRESS) {
        cameraPosition = Translate(vec3(0.0f, 0.0f, dcam)) * cameraPosition;
    }

    if (key == GLFW_KEY_LEFT && action == GLFW_PRESS) {
        cameraPosition = Translate(vec3(0.0f, -dcam, 0.0f)) * cameraPosition;
    }
    else if (key == GLFW_KEY_RIGHT && action == GLFW_PRESS) {
        cameraPosition = Translate(vec3(0.0f, dcam, 0.0)) * cameraPosition;
    }


}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
static void mouseClick(GLFWwindow* window, int button, int action, int mods){

    if (GLFW_RELEASE == action){
        GLState::moving=GLState::scaling=GLState::panning=false;
        return;
    }

    if( mods & GLFW_MOD_SHIFT){
        GLState::scaling=true;
    }else if( mods & GLFW_MOD_ALT ){
        GLState::panning=true;
    }else{
        GLState::moving=true;
        TrackBall::trackball(GLState::lastquat, 0, 0, 0, 0);
    }

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    GLState::beginx = xpos; GLState::beginy = ypos;

    std::vector < vec4 > ray_o_dir = findRay(xpos, ypos);
    castRayDebug(ray_o_dir[0], vec4(ray_o_dir[1].x, ray_o_dir[1].y, ray_o_dir[1].z,0.0));

}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
void mouseMove(GLFWwindow* window, double x, double y){

    int W, H;
    glfwGetFramebufferSize(window, &W, &H);


    float dx=(x-GLState::beginx)/(float)W;
    float dy=(GLState::beginy-y)/(float)H;

    if (GLState::panning)
    {
        GLState::ortho_x  +=dx;
        GLState::ortho_y  +=dy;

        GLState::beginx = x; GLState::beginy = y;
        return;
    }
    else if (GLState::scaling)
    {
        GLState::scalefactor *= (1.0f+dx);

        GLState::beginx = x;GLState::beginy = y;
        return;
    }
    else if (GLState::moving)
    {
        TrackBall::trackball(GLState::lastquat,
                             (2.0f * GLState::beginx - W) / W,
                             (H - 2.0f * GLState::beginy) / H,
                             (2.0f * x - W) / W,
                             (H - 2.0f * y) / H
                             );

        TrackBall::add_quats(GLState::lastquat, GLState::curquat, GLState::curquat);
        TrackBall::build_rotmatrix(GLState::curmat, GLState::curquat);

        GLState::beginx = x;GLState::beginy = y;
        return;
    }
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
void initGL(){

    GLState::light_ambient  = vec4(lightColor.x, lightColor.y, lightColor.z, 1.0 );
    GLState::light_diffuse  = vec4(lightColor.x, lightColor.y, lightColor.z, 1.0 );
    GLState::light_specular = vec4(lightColor.x, lightColor.y, lightColor.z, 1.0 );


    std::string vshader = source_path + "/shaders/vshader.glsl";
    std::string fshader = source_path + "/shaders/fshader.glsl";

    GLchar* vertex_shader_source = readShaderSource(vshader.c_str());
    GLchar* fragment_shader_source = readShaderSource(fshader.c_str());

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, (const GLchar**) &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);
    check_shader_compilation(vshader, vertex_shader);

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, (const GLchar**) &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);
    check_shader_compilation(fshader, fragment_shader);

    GLState::program = glCreateProgram();
    glAttachShader(GLState::program, vertex_shader);
    glAttachShader(GLState::program, fragment_shader);

    glLinkProgram(GLState::program);
    check_program_link(GLState::program);

    glUseProgram(GLState::program);

    glBindFragDataLocation(GLState::program, 0, "fragColor");

    // set up vertex arrays
    GLState::vPosition = glGetAttribLocation( GLState::program, "vPosition" );
    GLState::vNormal = glGetAttribLocation( GLState::program, "vNormal" );

    // Retrieve transformation uniform variable locations
    GLState::ModelView = glGetUniformLocation( GLState::program, "ModelView" );
    GLState::NormalMatrix = glGetUniformLocation( GLState::program, "NormalMatrix" );
    GLState::ModelViewLight = glGetUniformLocation( GLState::program, "ModelViewLight" );
    GLState::Projection = glGetUniformLocation( GLState::program, "Projection" );

    GLState::objectVao.resize(sceneObjects.size());
    glGenVertexArrays( sceneObjects.size(), &GLState::objectVao[0] );

    GLState::objectBuffer.resize(sceneObjects.size());
    glGenBuffers( sceneObjects.size(), &GLState::objectBuffer[0] );

    for(unsigned int i=0; i < sceneObjects.size(); i++){
        glBindVertexArray( GLState::objectVao[i] );
        glBindBuffer( GL_ARRAY_BUFFER, GLState::objectBuffer[i] );
        size_t vertices_bytes = sceneObjects[i]->mesh.vertices.size()*sizeof(vec4);
        size_t normals_bytes  =sceneObjects[i]->mesh.normals.size()*sizeof(vec3);

        glBufferData( GL_ARRAY_BUFFER, vertices_bytes + normals_bytes, NULL, GL_STATIC_DRAW );
        size_t offset = 0;
        glBufferSubData( GL_ARRAY_BUFFER, offset, vertices_bytes, &sceneObjects[i]->mesh.vertices[0] );
        offset += vertices_bytes;
        glBufferSubData( GL_ARRAY_BUFFER, offset, normals_bytes,  &sceneObjects[i]->mesh.normals[0] );

        glEnableVertexAttribArray( GLState::vNormal );
        glEnableVertexAttribArray( GLState::vPosition );

        glVertexAttribPointer( GLState::vPosition, 4, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0) );
        glVertexAttribPointer( GLState::vNormal, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(vertices_bytes));

    }



    glEnable( GL_DEPTH_TEST );
    glShadeModel(GL_SMOOTH);

    glClearColor( 0.8, 0.8, 1.0, 1.0 );

    //Quaternion trackball variables, you can ignore
    GLState::scaling  = 0;
    GLState::moving   = 0;
    GLState::panning  = 0;
    GLState::beginx   = 0;
    GLState::beginy   = 0;

    TrackBall::matident(GLState::curmat);
    TrackBall::trackball(GLState::curquat , 0.0f, 0.0f, 0.0f, 0.0f);
    TrackBall::trackball(GLState::lastquat, 0.0f, 0.0f, 0.0f, 0.0f);
    TrackBall::add_quats(GLState::lastquat, GLState::curquat, GLState::curquat);
    TrackBall::build_rotmatrix(GLState::curmat, GLState::curquat);

    GLState::scalefactor = 1.0;
    GLState::render_line = false;

}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
void drawObject(Object * object, GLuint vao, GLuint buffer){

    color4 material_ambient(object->shadingValues.color.x*object->shadingValues.Ka,
                            object->shadingValues.color.y*object->shadingValues.Ka,
                            object->shadingValues.color.z*object->shadingValues.Ka, 1.0 );
    color4 material_diffuse(object->shadingValues.color.x,
                            object->shadingValues.color.y,
                            object->shadingValues.color.z, 1.0 );
    color4 material_specular(object->shadingValues.Ks,
                             object->shadingValues.Ks,
                             object->shadingValues.Ks, 1.0 );
    float  material_shininess = object->shadingValues.Kn;

    color4 ambient_product  = GLState::light_ambient * material_ambient;
    color4 diffuse_product  = GLState::light_diffuse * material_diffuse;
    color4 specular_product = GLState::light_specular * material_specular;

    glUniform4fv( glGetUniformLocation(GLState::program, "AmbientProduct"), 1, ambient_product );
    glUniform4fv( glGetUniformLocation(GLState::program, "DiffuseProduct"), 1, diffuse_product );
    glUniform4fv( glGetUniformLocation(GLState::program, "SpecularProduct"), 1, specular_product );
    glUniform4fv( glGetUniformLocation(GLState::program, "LightPosition"), 1, lightPosition );
    glUniform1f(  glGetUniformLocation(GLState::program, "Shininess"), material_shininess );

    glBindVertexArray(vao);
    glBindBuffer( GL_ARRAY_BUFFER, buffer );
    glVertexAttribPointer( GLState::vPosition, 4, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0) );
    glVertexAttribPointer( GLState::vNormal, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(object->mesh.vertices.size()*sizeof(vec4)) );

    mat4 objectModelView = GLState::sceneModelView*object->getModelView();


    glUniformMatrix4fv( GLState::ModelViewLight, 1, GL_TRUE, GLState::sceneModelView);
    glUniformMatrix3fv( GLState::NormalMatrix, 1, GL_TRUE, Normal(objectModelView));
    glUniformMatrix4fv( GLState::ModelView, 1, GL_TRUE, objectModelView);

    glDrawArrays( GL_TRIANGLES, 0, object->mesh.vertices.size() );

}


int main(void){

    GLFWwindow* window;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_SAMPLES, 4);

    window = glfwCreateWindow(768, 768, "Raytracer", NULL, NULL);
    if (!window){
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseClick);
    glfwSetCursorPosCallback(window, mouseMove);


    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval(1);

    switch(scene){
    case _SPHERE:
        initUnitSphere();
        break;
    case _SQUARE:
        initUnitSquare();
        break;
    case _BOX:
        initCornellBox();
        break;
    case _BOXEASYSPHERE:
        initCornellBox2();
        break;
    }

    initGL();

    while (!glfwWindowShouldClose(window)){

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        GLState::window_height = height;
        GLState::window_width  = width;

        glViewport(0, 0, width, height);


        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mat4 track_ball =  mat4(GLState::curmat[0][0], GLState::curmat[1][0],
                GLState::curmat[2][0], GLState::curmat[3][0],
                GLState::curmat[0][1], GLState::curmat[1][1],
                GLState::curmat[2][1], GLState::curmat[3][1],
                GLState::curmat[0][2], GLState::curmat[1][2],
                GLState::curmat[2][2], GLState::curmat[3][2],
                GLState::curmat[0][3], GLState::curmat[1][3],
                GLState::curmat[2][3], GLState::curmat[3][3]);

        GLState::sceneModelView  =  Translate(-cameraPosition) *   //Move Camera Back
                Translate(GLState::ortho_x, GLState::ortho_y, 0.0) *
                track_ball *                   //Rotate Camera
                Scale(GLState::scalefactor,
                      GLState::scalefactor,
                      GLState::scalefactor);   //User Scale

        GLfloat aspect = GLfloat(width)/height;

        switch(scene){
        case _SPHERE:
        case _SQUARE:
            GLState::projection = Perspective( 45.0, aspect, 0.01, 100.0 );
            break;
        case _BOX:
            GLState::projection = Perspective( 45.0, aspect, 4.5, 100.0 );
            break;
        case _BOXEASYSPHERE:
            GLState::projection = Perspective(45.0, aspect, 4.5, 100.0);
            break;
        }

        glUniformMatrix4fv( GLState::Projection, 1, GL_TRUE, GLState::projection);

        for(unsigned int i=0; i < sceneObjects.size(); i++){
            drawObject(sceneObjects[i], GLState::objectVao[i], GLState::objectBuffer[i]);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

    }

    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}
