// Author: Ronaldo Engelke Cunha
// Class: CAP 5725
// Assignment: Assignment 6
// Date: 10/22/2022


#include <iostream>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/common.hpp>
#include <vector>
#include <Math.h>  
#include <cmath>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

//created headers
#include "shader.h"
#include "model.h"
#include "mesh.h"

#define Pi 3.1415926535897932384626433832795

typedef struct {
	std::vector<float> positions;
	std::vector<float> normals;
	std::vector<float> texCoords;
	std::vector<unsigned int> indices;
} SphereData;

//vertex shader - processes input vertex data, usually normalizes and transforms input data to coordinates that fall within OpenGL's visible region
std::string modelVertex = R"(#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
uniform mat4 view, projection, model;
out vec3 WorldPos, Normal;

void main()
{
	WorldPos = vec3(model * vec4(position, 1.0));
	Normal = mat3(model) * normal;
	gl_Position = projection * view * model * vec4(WorldPos, 1.0);
})";


std::string modelFragment = R"(#version 330 core
out vec4 FragColor;
in vec3 WorldPos;
in vec3 Normal;

uniform float roughness;

uniform samplerCube cubeMapTex;
uniform vec3 cameraPosition;
uniform vec3 F0;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// Geometry term (G)
float GeometrySchlickGGX(float NdotV, float k)
{
    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return nom / denom;
}
  
float GeometrySmith(vec3 N, vec3 V, vec3 L, float k)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, k);
    float ggx2 = GeometrySchlickGGX(NdotL, k);
	 
    return ggx1 * ggx2;
}

//Fresnel term (F)
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

//For generating a random (low-discrepancy) sequence value
float RadicalInverse_VdC(uint bits) 
{
     bits = (bits << 16u) | (bits >> 16u);
     bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
     bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
     bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
     bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
     return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
// ----------------------------------------------------------------------------
vec2 Hammersley(uint i, uint N)
{
	return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

//with a random sequence value as input, generate a sample vector in tangent space, transform to world space 
//and sample the scene's randiance 
vec3 ImportanceSample(vec2 sequence, vec3 N, float roughness)
{
    float a = roughness*roughness;
	
    float phi = 2.0 * PI * sequence.x;
    float cosTheta = sqrt((1.0 - sequence.y) / (1.0 + (a*a - 1.0) * sequence.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	
    // from spherical coordinates to cartesian coordinates
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
	
    // from tangent-space vector to world-space sample vector
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
	
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}  

float chiGGX(float v)
{
	float x = v > 0 ? 1 : 0;
    return x;
}

float GGX_PartialGeometryTerm(vec3 v, vec3 n, vec3 h, float alpha)
{
    float VdotH2 = max(dot(v,h), 0.0);
    float chi = chiGGX( VdotH2 / max(dot(v,n), 0.0) );
    VdotH2 = VdotH2 * VdotH2;
    float tan2 = ( 1 - VdotH2 ) / VdotH2;
    return (chi * 2) / ( 1 + sqrt( 1 + alpha * alpha * tan2 ) );
}


float G1_GGX_Schlick(float NdotV, float roughness) {
  float r = roughness; // original
  //float r = 0.5 + 0.5 * roughness; // Disney remapping
  float k = (r * r) / 2.0;
  float denom = NdotV * (1.0 - k) + k;
  return NdotV / denom;
}

float G_Smith(float NoV, float NoL, float roughness) {
  float g1_l = G1_GGX_Schlick(NoL, roughness);
  float g1_v = G1_GGX_Schlick(NoV, roughness);
  return g1_l * g1_v;
}



//completing entire integration of the BRDF equation over all sampled light vectors for this fragment
vec3 IntegrateBRDF(float NdotV, float roughness, vec3 normal, vec3 view)
{
	vec3 blue = vec3(0.2,0.1,0.9);
	vec3 specular = vec3(0.0);
    const uint SAMPLE_COUNT = 20u;

    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
		//vec3 halfVector = normalize(sampleVector + normal);

        vec2 sequence = Hammersley(i, SAMPLE_COUNT);
        vec3 halfVector = ImportanceSample(sequence, normal, roughness);
		vec3 light =  2 * dot( view, halfVector ) * halfVector - view;
		
		float NdotL = max(dot(normal, light), 0.001);
		float NdotH = max(dot(normal, halfVector), 0.0);
		float VdotH = max(dot(view, halfVector), 0.0);
		float HdotV = max(dot(view, halfVector), 0.0);
		float NdotV = max(dot(normal, view), 0.0);

		vec3 radiance = texture(cubeMapTex, light, 0.0).rgb; 
			
		float G = GeometrySmith(normal, view, light, roughness);
		vec3 F = fresnelSchlick(HdotV, F0); //careful here

		//float D = Distribution(normal, halfVector, roughness);
		//float denom  = 4.0 * NdotV * NdotL + 0.05;

		specular += radiance * F * G * NdotL / (NdotH * NdotV);
    }
	
	specular /= float(SAMPLE_COUNT);
	
    return specular;
}

void main()
{		
	vec3 N = Normal;
    vec3 V = normalize(cameraPosition - WorldPos);
    vec3 R = reflect(-V, N); 
	vec3 BRDF = IntegrateBRDF(max(dot(N, V), 0.0), roughness, N, V);


	float gamma = 2.2;
	//BRDF = pow(BRDF, vec3(1.0 / gamma));
	FragColor = vec4(BRDF, 1.0);
}  

)";

std::string skyboxVertexShader = R"(#version 330 core
layout (location = 0) in vec3 position;
out vec3 fragPosition;
uniform mat4 projection;
uniform mat4 view;
void main()
{
	fragPosition = position;
	vec4 pos = projection * view * vec4(position, 1.0);
	gl_Position = pos.xyww; 
})";

std::string skyboxFragmentShader = R"(#version 330 core
in vec3 fragPosition;
out vec4 outColor;
uniform samplerCube cubeMapTex;
uniform mat4 view, projection;
uniform vec3 cameraPosition;
void main()
{
	//vec4 farPlanePosition = inverse(view * projection) * vec4(fragPosition, 1, 1);
	//vec3 direction = farPlanePosition.xyz/farPlanePosition.w - cameraPosition;
	//outColor = texture(cubeMapTex, normalize(direction));
	outColor = texture(cubeMapTex, fragPosition);
})";

glm::vec3 cameraUp, cameraPos, cameraFront;
glm::vec3 cameraPosSkybox;
float yaw = -90.0f;
float pitch, Radius;
float camX, camY, camZ;

float skyboxVertices2[] = {
	// positions          
	-1.0f,  1.0f, -1.0f,
	-1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,
	 1.0f,  1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,

	-1.0f, -1.0f,  1.0f,
	-1.0f, -1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,
	-1.0f,  1.0f,  1.0f,
	-1.0f, -1.0f,  1.0f,

	 1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,

	-1.0f, -1.0f,  1.0f,
	-1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f, -1.0f,  1.0f,
	-1.0f, -1.0f,  1.0f,

	-1.0f,  1.0f, -1.0f,
	 1.0f,  1.0f, -1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	-1.0f,  1.0f,  1.0f,
	-1.0f,  1.0f, -1.0f,

	-1.0f, -1.0f, -1.0f,
	-1.0f, -1.0f,  1.0f,
	 1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,
	-1.0f, -1.0f,  1.0f,
	 1.0f, -1.0f,  1.0f
};

/// <summary>
/// Resize callback function for when window is resized by user. This adjusts the viewpoirt when the user resizes the window.
/// This function takes a window, and new window dimensions as parameters. Whenever the window changes in size, GLFW calls this function and fills in the proper arguments for you to process.
/// </summary>
/// <param name="window"></param>
/// <param name="width"></param>
/// <param name="height"></param>
void framebuffer_size_callback(GLFWwindow* window, int width, int height);

/// <summary>
/// Processes user input.
/// Processes user input.
/// </summary>
/// <param name="window"></param>
void processInput(GLFWwindow* window);

static float metallic = 0.95f;
static float roughness = 1 - metallic;


int main()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(800, 600, "LearnOpenGL", NULL, NULL);

	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window); //tell GLFW to make the context of our window the main context on the current thread

	if (glewInit() != GLEW_OK) //pass GLEW the function to load the address of the OpenGL function pointers which is OS-specific
	{
		std::cout << "Failed to initialize GLEW" << std::endl;
		return -1;
	}
	int width, height;
	width = 800;
	height = 600;
	glViewport(0, 0, width, height);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	//Skybox----------------------------------------------------------------------------------------------------
	unsigned int VAO_Skybox, VBO_Skybox;
	glGenVertexArrays(1, &VAO_Skybox);
	glBindVertexArray(VAO_Skybox);

	glGenBuffers(1, &VBO_Skybox);
	glBindBuffer(GL_ARRAY_BUFFER, VBO_Skybox);
	glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices2), &skyboxVertices2[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

	//load cube map texture---------------------------------------------------------------------------------------------------
	unsigned int textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	int imgWidth, imgHeight, nrChannels;
	unsigned char* data;

	const char* files[] = { "right.jpg" ,"left.jpg", "top.jpg", "bottom.jpg","front.jpg", "back.jpg", };
	for (int i = 0; i < 6; i++) {
		data = stbi_load(files[i], &imgWidth, &imgHeight, &nrChannels, 0);
		if (data)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, imgWidth, imgHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
			std::cout << "Cubemap texture " << files[i] << " loaded." << std::endl;
			stbi_image_free(data);
		}
		else
		{
			std::cout << "Cubemap texture " << files[i] << " failed to load." << std::endl;
		}
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);


	//camera----------------------------------------------------------------------------------------------------

	//perspective matrix
	float aspect = (float)width / (float)height;
	float near = 1, far = 100, fov = 45;
	//glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, near, far);

	//positioning
	cameraPos = glm::vec3(0.0f, 0.0f, 30.0f);

	cameraPosSkybox = glm::vec3(0.0f, 0.0f, 1.0f);

	//direction
	glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f); //point to the origin of our scene
	glm::vec3 cameraDirectionVec = glm::normalize(cameraPos - cameraTarget); //get the vector to point in the positive z 

	//specify the up vector
	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

	//right vector will be the cross product of our up vector and the direction vector
	glm::vec3 cameraRightVec = glm::normalize(glm::cross(up, cameraDirectionVec));

	//get the camera up vector, we have the right and direction vector, cross product will result in the up vec
	cameraUp = glm::cross(cameraDirectionVec, cameraRightVec);
	cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);

	//------------------------------------------------------------------------------------------------------------

	////reseting all bindings
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnable(GL_DEPTH_TEST);


	Shader skyboxShader(skyboxVertexShader, skyboxFragmentShader);

	skyboxShader.use();
	skyboxShader.setInt("cubeMapTex", 0);

	Shader modelShader1(modelVertex, modelFragment);

	Shader modelShader2(modelVertex, modelFragment);
	Shader modelShader3(modelVertex, modelFragment);

	Model backpack("Horse.obj");

	modelShader1.setInt("cubeMapTex", 0);
	modelShader2.setInt("cubeMapTex", 0);
	modelShader3.setInt("cubeMapTex", 0);

	
	//render loop
	while (!glfwWindowShouldClose(window))
	{
		//input
		processInput(window);

		//render
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		//create uniforms
		glm::mat4 view2 = glm::lookAt(cameraPosSkybox, glm::vec3(0.0f, 0.0f, 0.0f), cameraUp);
		glm::mat4 view1 = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), cameraUp);
		glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
		glm::mat4 model = glm::mat4(1.0f);
		//model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0, 0, 0));
		//model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0, 1.0, 0));
		//model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1.0));
		model = glm::translate(model, glm::vec3(-5.0f, -3.0f, 0.0f)); // translate it down so it's at the center of the scene
		model = glm::scale(model, glm::vec3(0.5f, 0.5f, 0.5f));	// it's a bit too big for our scene, so scale it down
		//model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0));
		float roughness = 0.05;
		glm::vec3 F0 = glm::vec3(1.00, 0.71, 0.29);

		//MODEL1
		modelShader1.use();
		modelShader1.setMat4("projection", projection);
		modelShader1.setMat4("view", view1);
		modelShader1.setMat4("model", model);
		modelShader1.setVec3("cameraPosition", cameraPos);
		modelShader1.setFloat("roughness", roughness);
		modelShader1.setVec3("F0", F0);

		backpack.Draw(modelShader1);

		F0 = glm::vec3(0.56, 0.57, 0.58); //iron
		modelShader2.use();
		roughness = 0.05;
		model = glm::translate(model, glm::vec3(10.0f, 0.0f, 0.0f)); // translate it down so it's at the center of the scene
		modelShader2.setMat4("projection", projection);
		modelShader2.setMat4("view", view1);
		modelShader2.setMat4("model", model);
		modelShader2.setVec3("cameraPosition", cameraPos);
		modelShader2.setFloat("roughness", roughness);
		modelShader2.setVec3("F0", F0);

		backpack.Draw(modelShader2);

		F0 = glm::vec3(0.95, 0.93, 0.88); //silver
		modelShader3.use();
		roughness = 0.05;
		model = glm::translate(model, glm::vec3(10.0f, 0.0f, 0.0f)); // translate it down so it's at the center of the scene
		modelShader3.setMat4("projection", projection);
		modelShader3.setMat4("view", view1);
		modelShader3.setMat4("model", model);
		modelShader3.setVec3("cameraPosition", cameraPos);
		modelShader3.setFloat("roughness", roughness);
		modelShader3.setVec3("F0", F0);

		backpack.Draw(modelShader3);

		//skybox rendering------------------------------------------------------------------------------------
		glDepthFunc(GL_LEQUAL); // set depth function back to default
		skyboxShader.use();

		skyboxShader.setMat4("view", view2);
		skyboxShader.setMat4("projection", projection);
		skyboxShader.setVec3("cameraPosition", cameraPos);

		//bindings
		glBindVertexArray(VAO_Skybox);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
		glDrawArrays(GL_TRIANGLES, 0, 36);

		glBindVertexArray(0); //reset binding
		glDepthFunc(GL_LESS);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	//now delete the individual shader objects once we've linked them into the program object, we don't need them anymore
	//glDeleteShader(vertexShader);
	//glDeleteShader(fragmentShader);

	glDeleteVertexArrays(1, &VAO_Skybox);
	glDeleteBuffers(1, &VBO_Skybox);

	glfwTerminate();
	return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window)
{
	const float cameraSpeed = 0.5f; // adjust accordingly

		//ESC
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;
}
