//////////////////////////////////////////////////////////////////////////////
//
//  --- Object.cpp ---
//  Created by Brian Summa
//
//////////////////////////////////////////////////////////////////////////////


#include "common.h"

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
Object::IntersectionValues Sphere::intersect(vec4 p0, vec4 V){
  IntersectionValues result;
  //TODO: Ray-sphere setup

  /*typedef struct {
      double t; // time 
      vec4 P; // point of intersection 
      vec4 N; // normal at P 
      int ID_; // Id of object 
      std::string name; // name of object 
  } IntersectionValues;
  */
  result.ID_ = -1; 
  result.t = this->raySphereIntersection(p0, V); 
  result.name = this->name; 
  result.N = vec4(1.0, 0.0, 0.0, 1.0);

  if (result.t < (std::numeric_limits < double > ::max)())
  {
      // r(t) = o + t*d
      result.P = p0 + result.t * V;
      // normal = vec center point 
      result.N = result.P - this->center;
      result.N = Angel::normalize(result.N); 
  }

  return result;
}

/* -------------------------------------------------------------------------- */
/* ------ Ray = p0 + t*V  sphere at origin center and radius radius    : Find t ------- */
double Sphere::raySphereIntersection(vec4 p0, vec4 V){
  double t   = std::numeric_limits< double >::infinity();
  //TODO: Ray-sphere intersection;

  // t² d*d + 2t d*(origin rayon - center) + (norm(no-c)² - r²v
 
  double c = Angel::dot(p0 - this->center, p0 - this->center) - this->radius* this->radius; // square norm 
  double a = Angel::dot(V, V); 
  double b = 2.0 * Angel::dot(V, p0 - this->center); 
  double delta = b*b - (4 * a * c) ; 
  if (delta < 0.0) 
  {
      return t; 
  }
  else if (std::fabs(delta) < Angel::DivideByZeroTolerance) // Take epsilon from Vec 1e-12
  {
      t = -b / (2.0 * a); 
  }
  else
  {
      double t1 = (-b + std::sqrt(delta)) / (2.0 * a); 
      double t2 = (-b - std::sqrt(delta)) / (2.0 * a); 
      // Keep first one to appear 

      if (t1 > 0.0 && t2 > 0.0) 
      {
          t = min(t1, t2);
      }
      else
      {
          t = max(t1, t2); 
      }

      
  }

  return t;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
Object::IntersectionValues Square::intersect(vec4 p0, vec4 V){
  IntersectionValues result;
  //TODO: Ray-square setup

  result.ID_ = -1;
  result.t = this->raySquareIntersection(p0, V);
  result.name = this->name;
  result.N = vec4(this->normal, 0.0);
  // r(t) = o + t*d
  result.P = p0 + result.t * V;

  // inside square 
  bool inside = insideSquare(result.P); 
  if (! inside) {
      result.t = std::numeric_limits< double >::infinity();
  }

  std::string msg = "Inside =" + std::to_string(int(inside)) + "\n";
  OutputDebugString(msg.c_str());

  
  return result;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
double Square::raySquareIntersection(vec4 p0, vec4 V){
  double t   = std::numeric_limits< double >::infinity();
  vec4 N = vec4(this->normal, 0.0); 
  //TODO: Ray-square intersection;
  // t = (D - p0.n) / (V.n)  
  // t = (a - o).n / d.n 

  
  if (std::fabs(Angel::dot(V, N)) < Angel::DivideByZeroTolerance) {
      return t; 
  }

  // Plane intersection
  double D = Angel::dot(this->point, N);
  t = (D - Angel::dot(p0, N)) / Angel::dot(V, N);

  /*std::string msg = "D=" + std::to_string(D) + "\n"; 
  OutputDebugString(msg.c_str()); 
  msg = "o.n = " + std::to_string(Angel::dot(p0, N)) + "\n";
  OutputDebugString(msg.c_str());
  msg = "d.n = " + std::to_string(Angel::dot(V, N)) + "\n";
  OutputDebugString(msg.c_str());*/

  

  return t;
}

double Square::signedTrigArea(const vec4& a, const vec4& b, const vec4& c) const
{
    vec3 crossprod = Angel::cross(b - a, c - a);
    return 0.5 * crossprod.z;
}

bool Square::insideTriangle(const vec4& a, const vec4& b, const vec4& c, const vec4& p ) const
{
    double aireABC= signedTrigArea(a, b, c);
    double alpha  = signedTrigArea(p, b, c) / aireABC;
    double beta   = signedTrigArea(a, p, c) / aireABC;
    double gamma  = signedTrigArea(a, b, p) / aireABC;

    return alpha >= 0.0 && beta >= 0.0 && gamma >= 0.0;
}

bool Square::insideSquare(const vec4& p) const
{
    // Inside Square / not triangle
    bool trigABC = insideTriangle(mesh.vertices[0], mesh.vertices[1], mesh.vertices[2], p); 
    bool trigDEF = insideTriangle(mesh.vertices[3], mesh.vertices[4], mesh.vertices[5], p); 


    return trigABC || trigDEF;
}
