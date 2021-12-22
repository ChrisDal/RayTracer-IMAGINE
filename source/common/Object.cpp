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
  result.N = this->normal;
  // r(t) = o + t*d
  result.P = p0 + result.t * V;
  
  return result;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
double Square::raySquareIntersection(vec4 p0, vec4 V){
  double t   = std::numeric_limits< double >::infinity();
  //TODO: Ray-square intersection;

  
  if (std::fabs(Angel::dot(V, this->normal)) < Angel::DivideByZeroTolerance) {
      return t; 
  }

  double D = Angel::dot(this->point, this->normal);
  t = (D - Angel::dot(p0, this->normal)) / Angel::dot(V, this->normal);

  return t;
}
