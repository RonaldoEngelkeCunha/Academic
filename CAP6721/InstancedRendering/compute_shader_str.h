#ifndef COMPUTE_SHADER_STR_H
#define COMPUTE_SHADER_STR_H

#include <iostream>

std::string computeShaderStr = R"(
#version 430
#define EPSILON 0.0001
#define SIZE 9

layout(local_size_x = 10, local_size_y = 10, local_size_z = 1) in;

layout(rgba32f, binding = 0) uniform image2D img_out;

struct Molecule {
	vec3 C;	//center
	float R;	//radius
	vec3 RGB;	//color
} molecules[SIZE];

struct Ray {
	vec3 O;
	vec3 d;
	vec3 invdir;
	float tmin, tmax;
};

struct Box
{
	vec3 cubeMin;
	vec3 cubeMax;
	vec3 color;
	mat4 transMatrix;
};

struct Intersection
{
	vec3 intersectionPoint;
	vec3 normal;
};

layout(std430, binding = 3) buffer positions_buf{
	float positions[];
};

layout(std430, binding = 4) buffer radii_buf{
	float radii[];
};

layout(std430, binding = 5) buffer color_buf{
	float colors[];
};

layout(std430, binding = 6) buffer boxMinBuf{
	float boxMin[];
};

layout(std430, binding = 7) buffer boxMaxBuf{
	float boxMax[];
};

uniform int moleculeCount;
uniform int max_iter;
uniform vec2 center;

uniform mat4 tr1;
uniform mat4 tr2;
uniform mat4 tr3;
uniform mat4 tr4;
uniform mat4 tr5;

uniform vec3 eye, at, up;
uniform float fov; //in radian
uniform vec3 light;

struct Attr {
	float t;
	vec3 intersectionPoint;
	vec3 normal;
	vec3 materialColor;
	vec3 finalColor;
};

//globals
vec2 resolution = vec2(imageSize(img_out));
float infinity = 1.0 / 0.0;
vec3 currentPix;
vec3 boxMinCoords = vec3(boxMin[0], boxMin[1], boxMin[2]);
vec3 boxMaxCoords = vec3(boxMax[0], boxMax[1], boxMax[2]);
vec3 cubeMin = vec3(-1,-1,-1);
vec3 cubeMax = vec3(1,1,1);
mat4 transformations [5] = {tr1, tr2, tr3, tr4, tr5};
Box boxes[5];

//function prototypes
Ray rayGenerate(vec2 pixel);
Ray generateTransformedRay(Ray ray, mat4 trMat);
vec3 pixelColor(vec2 pixel);
vec3 traceRay(in Ray ray, vec2 pixel);
bool intersectSphere(in Ray ray, in Molecule molecule, inout Attr attr);
vec3 miss(vec2 pixel);
bool intersectsAABB(in Ray ray);
bool intersectsCAAB(inout Ray invRay, inout Ray ray, mat4 trans, inout Attr attr, in int closestWall);
void loadMolecules();
vec3 getCaabColor(int i);
bool traceRay(in Ray ray, inout Attr attr);
vec3 calculate_normal(vec3 point, vec3 cube_min, vec3 cube_max);
bool isShadowed(in Attr attr);

void main()
{
	loadMolecules();
	vec2 pixel = gl_GlobalInvocationID.xy;
	vec3 pixel_color = pixelColor(pixel);
	vec4 color = vec4(pixel_color, 1.0);
	imageStore(img_out, ivec2(pixel), color);
}

vec3 pixelColor(vec2 pixel)
{
	Ray ray = rayGenerate(pixel);
	return traceRay(ray, pixel);
}


vec3 traceRay(in Ray ray, vec2 pixel)
{
	Attr attr;
	if(traceRay(ray, attr))
		return attr.finalColor;
	else
		return miss(pixel);
}

bool traceRay(in Ray ray, inout Attr attr)
{
	bool hitSphere = false;
	bool hitPrism = false;
	Molecule closestMolecule;
	int closestWall;
	float closestDistance = infinity;
	attr.t = infinity;

	if(intersectsAABB(ray))
	{
		//Molecules
		for(int j=0; j<9; j++)
		{
			if(intersectSphere(ray,molecules[j], attr))
			{
				closestMolecule = molecules[j];
				hitSphere = true;
			}
		}

		//CAAB
		for(int i=0; i<5; i++)
		{
			Ray invRay = generateTransformedRay(ray, transformations[i]);
			if(intersectsCAAB(invRay, ray, transformations[i], attr, i))
			{
				closestWall = i;
				hitPrism = true;
			}
		}
		
	}//end intersectsAABB

	if(hitSphere == false && hitPrism == false)
		return false;


	//Apply lighting
	if(!isShadowed(attr))
	{
		//if(hitSphere)
		//{
		//	vec3 MaterialColor = closestMolecule.RGB.rgb;
		//	vec3 L = normalize(light);
		//	vec3 N = normalize(attr.normal);
		//	vec3 Color =  MaterialColor * clamp(dot(N, L), 0, 1);
		//	attr.materialColor = Color;
		//}
		//else
		//{
		//	vec3 L = normalize(light);
		//	vec3 N = normalize(attr.normal);
		//	vec3 MaterialColor = getCaabColor(closestWall);
		//	vec3 Color = MaterialColor * clamp(dot(attr.normal, L), 0, 1);
		//	attr.materialColor = Color;
		//}

		vec3 L = normalize(light);
		vec3 N = normalize(attr.normal);
		vec3 Color = attr.materialColor * clamp(dot(attr.normal, L), 0, 1);
		attr.finalColor = Color;
	}
	else
	{
		attr.finalColor = attr.materialColor;
	}

	

	return true;
}

bool intersectsCAAB(inout Ray invRay, inout Ray ray,  mat4 trans , inout Attr attr, in int closestWall)
{
	vec3 bmin = vec3(-1,-1,-1);
	vec3 bmax = vec3(1,1,1);
	vec3 tmin = (bmin - invRay.O) * invRay.invdir;
	vec3 tmax = (bmax - invRay.O) * invRay.invdir;

	vec3 t1 = min(tmin, tmax);
	vec3 t2 = max(tmin, tmax);

	float tNear = max(max(t1.x, t1.y), t1.z);
	float tFar = min(min(t2.x, t2.y), t2.z);

	if(tNear > ray.tmax)
		return false; 

	if(tNear > tFar)
		return false;
	
	if(ray.tmax < tNear)
		return false;
	
	if(attr.t < tNear)
		return false;

	attr.t = tNear;
	attr.intersectionPoint = ray.O + tNear * ray.d;
	ray.tmax = tNear;
	attr.materialColor = getCaabColor(closestWall);

	vec3 normal;
	if(t1.x > t1.y && t1.x > t1.z)
	{
		normal = vec3(-1.0, 0.0, 0.0);
	} else if (t1.y > t1.z)
	{	
		normal = vec3(0.0, -1.0, 0.0);
	}else
	{
		normal = vec3(0.0, 0.0, -1.0);
	}

	if(t2.x < t2.y && t2.x < t2.z)
	{
		normal = vec3(1.0, 0.0, 0.0);
	} else if (t2.y < t2.z)
	{	
		normal = vec3(0.0, 1.0, 0.0);
	}else
	{
		normal = vec3(0.0, 0.0, 1.0);
	}

	//now inverse transpose normal with trans matrix
	mat4 normalMatrix = transpose(inverse(trans));
	normal = normalize((normalMatrix*vec4(normal,0)).xyz);

	attr.normal = normal;	
	return true;
}

bool intersectSphere(in Ray ray, in Molecule molecule, inout Attr attr)	
{
	float a = dot(ray.d, ray.d);
	vec3 o_c = ray.O - molecule.C.xyz;
	float b = 2.0 * dot(ray.d, o_c);
	float c = dot(o_c, o_c) - (molecule.R * molecule.R);
	float discriminant = b*b - 4.0 * a * c;

	//no real solutions
	if(discriminant < 0.0)
		return false;

	//now we can have disc = 0 (one real solution) or disc > 0 (two real solutions)

	float t; 
	t = (-b - sqrt(discriminant)) / (2.0*a);
	
	if(t <= ray.tmin)
		t =  (-b + sqrt(discriminant)) / (2.0*a);
	
	if(t <= ray.tmin || t >= ray.tmax) 
		return false;

	if(ray.tmax < t)
		return false; 

	if(attr.t < t)
		return false;

	attr.t = t;
	attr.intersectionPoint =  ray.O + attr.t * ray.d;
	attr.normal = normalize(attr.intersectionPoint - molecule.C.xyz);
	attr.materialColor = molecule.RGB.rgb;

	ray.tmax = t;
	return true;
}

bool isShadowed(in Attr attr)
{
	Ray shadowRay;
	shadowRay.O = attr.intersectionPoint;	
	shadowRay.d = normalize(light - attr.intersectionPoint);
	shadowRay.tmax = attr.t;
	shadowRay.tmin = 0;
	
	//Molecules
	for(int j=0; j<9; j++)
	{
		if(intersectSphere(shadowRay,molecules[j], attr))
		{
			return true;
		}
	}

	//CAAB
	for(int i=0; i<5; i++)
	{
		Ray invRay = generateTransformedRay(shadowRay, transformations[i]);
		if(intersectsCAAB(invRay, shadowRay, transformations[i], attr, i))
		{
			return true;
		}
	}
	
	return false;
}

bool intersectsAABB(in Ray ray)
{
	vec3 boundMin = vec3(-4.66, -4.72, -4.42);
	vec3 boundMax = vec3(4.45, 4.4, 4.42);

	//compute the extended AABB bounds for the molecule
	boundMin = boundMin * 2;
	boundMax = boundMax * 2;

	vec3 tmin = (boundMin - ray.O) * ray.invdir;
	vec3 tmax = (boundMax - ray.O) * ray.invdir;

	vec3 t1 = min(tmin, tmax);
	vec3 t2 = max(tmin, tmax);

	float tNear = max(max(max(t1.x, t1.y), t1.z), ray.tmin);
	float tFar = min(min(min(t2.x, t2.y), t2.z), ray.tmax);

	bool intersection = false;

	if(tNear <= tFar)
		intersection = true;

	return intersection;
}

vec3 getCaabColor(int i)
{
	vec3 materialColor;

	if(i==1)
		materialColor = vec3(1.0, 0.0, 0.0);
	else if(i==0)
		materialColor = vec3(0.0, 0.0, 1.0);
	else if(i==3)
		materialColor = vec3(0.0, 1.0, 0.0);
	else if(i==2)
		materialColor = vec3(0.0, 1.0, 0.0);
	else
		materialColor = vec3(0.67, 0.67, 0.67);

	return materialColor;
}

Ray rayGenerate(vec2 pixel)
{
	Ray ray;
	vec3 w = normalize(eye-at);
	vec3 u = normalize(cross(up,w));
	vec3 v = cross(w,u);
	float height = 2.0*tan(fov/2.0);

	float width = height * float(resolution.x) / float(resolution.y);
	ray.O = eye;

	ray.d = (
		-w
		+ (2.0 * pixel.y/resolution.y - 1.0) * (height / 2.0) * v
		+ (2.0 * pixel.x/resolution.x - 1.0) * (width / 2.0) * u
	);

	ray.tmin = 0.0; 
	ray.tmax = infinity;
	ray.invdir = 1 / ray.d;
	
	return ray;
}

Ray generateTransformedRay(Ray ray, mat4 trMat)
{
	mat4 invTr = inverse(trMat);
	Ray transRay; 
	transRay.O = vec3(invTr * vec4(ray.O, 1.0));
	transRay.d = vec3(invTr * vec4(ray.d, 0.0));
	transRay.tmin = 0.0;
	transRay.tmax = ray.tmax;
	transRay.invdir = 1/transRay.d;
	return transRay;
}



vec3 miss( vec2 pixel)
{
	//vec2 norm_coordinates = pixel / vec2(imageSize(img_out));
	//return vec3(norm_coordinates.x, norm_coordinates.y, 0.0);
	return vec3(0.0, 0.0, 0.0);
}


void loadMolecules()
{
	//for(int i=0, j=0, l=0; j<27; i++)
	//{
	//	Molecule mol;
	//	mol.C = vec3(positions[j++], positions[j++], positions[j++]);
	//	mol.RGB = vec3(colors[l++], colors[l++], colors[l++]);
	//	molecules[i] = mol;
	//}

	////now load radii
	//for(int i=0; i<9; i++)
	//{
	//	molecules[i].R = radii[i];
	//}

	Molecule mol0 = Molecule(vec3(0.01, -0.56, 0.0), 0.67 , vec3(0.565,0.565,0.565));
	Molecule mol1 = Molecule(vec3(-1.28, 0.24, 0.0), 0.67 , vec3(0.565,0.565,0.565));
	Molecule mol2 = Molecule(vec3(1.92, -0.22, 0.0), 0.53 , vec3(1,1,1));
	Molecule mol3 = Molecule(vec3(1.13, 0.32, 0.0), 0.48 , vec3(1,0.05,0.05));
	Molecule mol4 = Molecule(vec3(0.04, -1.19, 0.89), 0.53 , vec3(1,1,1));
	Molecule mol5 = Molecule(vec3(0.04, -1.19, -0.89), 0.53 , vec3(1,1,1));
	Molecule mol6 = Molecule(vec3(-2.13, -0.43, 0.0), 0.53 , vec3(1,1,1));
	Molecule mol7 = Molecule(vec3(-1.32, 0.87, 0.89), 0.54 , vec3(1,1,1));
	Molecule mol8 = Molecule(vec3(-1.32, 0.87, -0.89), 0.53 , vec3(1,1,1));

	molecules[0] = mol0;
	molecules[1] = mol1;
	molecules[2] = mol2;
	molecules[3] = mol3;
	molecules[4] = mol4;
	molecules[5] = mol5;
	molecules[6] = mol6;
	molecules[7] = mol7;
	molecules[8] = mol8;

}
)";

#endif