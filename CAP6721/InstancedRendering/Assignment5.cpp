
#include <stdio.h>
#include <stdlib.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/common.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <cstdio>
#include "compute_shader.h"
#include "general_shader.h"
#include "compute_shader_str.h"
#include "raster_shaders_str.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"
#include "JsonReader.h"
#include "windows.h"
#include <cstdlib>

GLFWwindow* window;
using namespace glm;
using namespace rapidjson;

int initializeLibraries();
void createImageStore();
void setupCamera();
void readjustCamera();
void draw();
int getPositionSize(const rapidjson::Value& value);
void processInput(GLFWwindow* window);
std::string readShaderSource(const char* filePath);

/// <summary>
/// A singular molecule within a compound 
/// </summary>
struct Molecule {
	float C[3];
	float R;	//radius
	float RGB[3];
};

struct AABB {
	float AABBmin[3];
	float AABBmax[3];
};

struct Square {
	vec2 squareMin;
	vec2 squareMax;
};

/// <summary>
/// A compound (list of molecules, e.g. ethanol)
/// </summary>
struct Compounds {
	Molecule compound[];
};

std::vector<std::string> compoundNames = { "Ethanol", "Aspirin", "Caffeine", "Nicotine", "LSD" };

//Compounds compoundList[17];

//used to send to the SSBO (this is a compound essentially)
std::vector<Molecule> molecules;
std::vector<AABB> boundingVector;

mat4 trans1, trans2, trans3, trans4, trans5;

GLint compute_work_group_size[3];
GLuint computeProgram = 0;
GLuint drawProgram = 0;
GLuint texture;
GLuint texture_out;
GLuint moleculesBuffer = 0;
GLuint boundingBuffer = 0;
GLuint positionsBuffer = 0;
GLuint colorBuffer = 0;
GLuint radiiBuffer = 0;
GLuint boxMinBuffer = 0;
GLuint boxMaxBuffer = 0;
GLuint randBuffer = 0;
GLuint previousImage = 0;

float seeds[20] = {
	0.1, 0.2,
	0.3, 0.4,
	0.5, 0.6,
	0.7, 0.8,
	0.9, 0.1,
	0.2, 0.3,
	0.4, 0.5,
	0.6, 0.7,
	0.8, 0.9,
	0.0, 1.0
};

//camera
glm::vec3 cameraUp, cameraFront, cameraTarget;
float fov = 45.0;
float camX, camY, camZ;
glm::mat4 projection;
float yaw = 90.0f;
float pitch, Radius;

//lighting
//glm::vec3 lightPos = glm::vec3(10.0f, 6.0f, 10.0f);
glm::vec3 lightPos = glm::vec3(0.0, 3.0, 2.0f);
//glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 16.0f);
glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 13.0f);
const float cameraSpeed = 0.3f; // adjust accordingly

unsigned int vao = 0, vbo = 0;
int invocationNumber = 1;
//uniforms and other valuables
int workgroups[3];
int WindowWidth = 800, WindowHeight = 800;
// texture size
const unsigned int TEXTURE_WIDTH = 1600, TEXTURE_HEIGHT = 1600; //1000 per
float scale = 2.0f / WindowHeight; //0.00416666666

glm::vec2 center = glm::vec2(WindowWidth * 0.75, WindowHeight * 0.5);
int max_iter = 100;

int main(void)
{
	//generating a random number, sampling from 0 to 224 (225 samples)
	srand((unsigned)time(NULL));
	int rand1;
	int rand2;


	FILE* fp = fopen("molecules.json", "rb");

	if (!fp) {
		std::cerr << "Error: coule not open file"
			<< std::endl;
		return 1;
	}

	char readBuffer[65536];
	rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));

	rapidjson::Document doc;
	doc.ParseStream(is);

	if (doc.HasParseError()) {
		std::cerr << "Error: failed to parse JSON document"
			<< std::endl;
		fclose(fp);
		return 1;
	}
	fclose(fp);
	Molecule mol;
	const Value& single_compound = doc["Ethanol"];

	const Value& quads = single_compound["quadruples"];
	assert(quads.IsArray());

	const int positionsSize = (quads.Size() / 4) * 3;
	std::vector<float> Positions(positionsSize);

	const int radiiSize = quads.Size() - positionsSize;
	std::vector<float> Radii(radiiSize);

	for (SizeType i = 0, j = 0, l = 0; i < quads.Size();)
	{
		Positions[j++] = quads[i++].GetFloat();
		Positions[j++] = quads[i++].GetFloat();
		Positions[j++] = quads[i++].GetFloat();
		Radii[l++] = quads[i++].GetFloat();
	}

	const Value& colors = single_compound["color"];
	assert(colors.IsArray());

	std::vector<float> Colors(colors.Size());

	for (SizeType i = 0, j = 0, l = 0; l < colors.Size(); i++)
	{
		Colors[l++] = colors[j++].GetInt() / 255;
		Colors[l++] = colors[j++].GetInt() / 255;
		Colors[l++] = colors[j++].GetInt() / 255;
	}

	const Value& box = single_compound["box"];
	const Value& min = box["min"];
	const Value& max = box["max"];

	assert(min.IsArray());
	assert(max.IsArray());

	std::vector<float> boxMin(3);
	std::vector<float> boxMax(3);

	AABB boundingBox;
	for (SizeType i = 0; i < min.Size(); i++)
	{
		//grab both bounds at the same time
		boundingBox.AABBmin[i] = min[i].GetFloat();
		boundingBox.AABBmax[i] = max[i].GetFloat();

		boxMin[i] = min[i].GetFloat();
		boxMax[i] = max[i].GetFloat();
	}

	boundingVector.push_back(boundingBox);

	//for testing purposes: use Ethanol

	int success = initializeLibraries();

	//Determine GPU Capability - determine the total count of work group in each dimmension, work group size, and total max invocation per work group
	int max_compute_work_group_count[3];
	int max_compute_work_group_size[3];
	int max_compute_work_group_invocations;

	for (int idx = 0; idx < 3; idx++) {
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, idx, &max_compute_work_group_count[idx]);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, idx, &max_compute_work_group_size[idx]);
	}
	glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &max_compute_work_group_invocations);

	std::cout << "OpenGL Limitations: " << std::endl;
	std::cout << "maxmimum number of work groups in X dimension " << max_compute_work_group_count[0] << std::endl;
	std::cout << "maxmimum number of work groups in Y dimension " << max_compute_work_group_count[1] << std::endl;
	std::cout << "maxmimum number of work groups in Z dimension " << max_compute_work_group_count[2] << std::endl;

	std::cout << "maxmimum size of a work group in X dimension " << max_compute_work_group_size[0] << std::endl;
	std::cout << "maxmimum size of a work group in Y dimension " << max_compute_work_group_size[1] << std::endl;
	std::cout << "maxmimum size of a work group in Z dimension " << max_compute_work_group_size[2] << std::endl;

	std::cout << "Number of invocations in a single local work group that may be dispatched to a compute shader " << max_compute_work_group_invocations << std::endl << std::endl;


	const char* filePath = "compute_shader.glsl";
	std::ifstream file("compute_shader.glsl");
	std::string computeShaderSource = readShaderSource(filePath);

	GeneralShader quadShader(vertShaderStr, fragShaderStr);
	ComputeShader computeShader(computeShaderSource);

	quadShader.use();
	quadShader.setInt("myTexture", 0);

	createImageStore();
	setupCamera();

	//incoming texture to be given to the shader for next iteration
	int dataSize = WindowWidth * WindowHeight * 4 * sizeof(float);
	float* pixels = new float[dataSize];

	bool firstRun = true;

	//glGenTextures(1, &previousImage);
	//glActiveTexture(GL_TEXTURE0);
	//glBindTexture(GL_TEXTURE_2D, previousImage);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WindowWidth, WindowHeight, 0, GL_RGBA, GL_FLOAT, NULL);

	//glBindImageTexture(1, previousImage, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
	//glActiveTexture(GL_TEXTURE0);
	//glBindTexture(GL_TEXTURE_2D, previousImage);


	glGenBuffers(1, &boundingBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, boundingBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, boundingVector.size() * sizeof(AABB), boundingVector.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, boundingBuffer);

	glGenBuffers(1, &positionsBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, positionsBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, positionsSize * sizeof(float), Positions.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, positionsBuffer);

	glGenBuffers(1, &radiiBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, radiiBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, radiiSize * sizeof(float), Radii.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, radiiBuffer);

	glGenBuffers(1, &colorBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, colorBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, colors.Size() * sizeof(float), Colors.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, colorBuffer);

	glGenBuffers(1, &boxMinBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, boxMinBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * sizeof(float), boxMin.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, boxMinBuffer);

	glGenBuffers(1, &boxMaxBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, boxMaxBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * sizeof(float), boxMax.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, boxMaxBuffer);

	glGenBuffers(1, &randBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, randBuffer);



	trans5 = glm::translate(glm::scale(mat4(1.0f), glm::vec3(5, 5, 0.01)), glm::vec3(0., 0., -400)); //back
	trans1 = glm::translate(glm::scale(mat4(1.0f), glm::vec3(0.1, 5, 5)), glm::vec3(50, 0.0, 0)); //right
	trans2 = glm::translate(glm::scale(mat4(1.0f), glm::vec3(0.1, 5, 5)), glm::vec3(-50, 0.0, 0)); //left
	trans3 = glm::translate(glm::scale(mat4(1.0f), glm::vec3(5, 0.1, 5)), glm::vec3(0.0, 50.5, 0.0)); //bottom
	trans4 = glm::translate(glm::scale(mat4(1.0f), glm::vec3(5, 0.1, 5)), glm::vec3(0.0, -50.5, 0.0)); //top

	GLuint previousImageUnit = 0;
	int rand3, rand4;
	int previousNumRuns = 0;
	const int fedRands = 1;
	int rands[fedRands];

	do {

		glClear(GL_COLOR_BUFFER_BIT);

		for (int i = 0, interval = 0; i < fedRands; i++)
		{
			int currentRand = rand() % 225;
			rands[interval++] = currentRand;
		}

		glBufferData(GL_SHADER_STORAGE_BUFFER, fedRands * sizeof(int), rands, GL_STATIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, randBuffer);

		float timeValue = glfwGetTime();

		computeShader.use();
		glUniform2fv(glGetUniformLocation(computeShader.ID, "seeds"), 10, &seeds[0]);
		computeShader.setFloat("time", timeValue);
		computeShader.setInt("invocationNumber", invocationNumber);
		computeShader.setMat4("tr1", trans1);
		computeShader.setMat4("tr2", trans2);
		computeShader.setMat4("tr3", trans3);
		computeShader.setMat4("tr4", trans4);
		computeShader.setMat4("tr5", trans5);
		computeShader.setInt("max_iter", max_iter);
		computeShader.setVec2("center", center);
		computeShader.setMat4("projection", projection);
		computeShader.setVec3("eye", cameraPos);
		computeShader.setVec3("at", cameraTarget);
		computeShader.setVec3("up", cameraUp);
		computeShader.setFloat("fov", fov);
		computeShader.setInt("prevNumRuns", previousNumRuns);

		invocationNumber++;
		//readjustCamera();
		//processInput(window);

		glBindImageTexture(0, texture, 0, GL_FALSE, 0,
			GL_READ_WRITE, GL_RGBA32F);

		//glDispatchCompute(workgroups[0], workgroups[1], 1);
		glDispatchCompute((unsigned int)TEXTURE_WIDTH / 10, (unsigned int)TEXTURE_HEIGHT / 10, 1);

		// make sure writing to image has finished before read
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		quadShader.use();
		draw();

		// Swap buffers
		glfwSwapBuffers(window);

		//poll
		glfwPollEvents();

		firstRun = false;

		previousNumRuns++;

	} // Check if the ESC key was pressed or the window was closed
	while (glfwGetKey(window, GLFW_KEY_ESCAPE) != GLFW_PRESS &&
		glfwWindowShouldClose(window) == 0);

	//clean up
	glDeleteTextures(1, &texture);
	glDeleteProgram(quadShader.ID);
	glDeleteProgram(computeShader.ID);

	glfwTerminate();
	return 0;
}
void processInput(GLFWwindow* window)
{

	//ESC
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
	//W
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
	{
		pitch += (1.0f * cameraSpeed);
	}
	//S
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
	{
		pitch -= (1.0f * cameraSpeed);
	}
	//A
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
	{
		yaw += (1.0f * cameraSpeed);
	}
	//D
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
	{
		yaw -= (1.0f * cameraSpeed);
	}

	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	camX = (cos(glm::radians(yaw)) * cos(glm::radians(pitch))) * Radius * 3.0f;
	camY = sin(glm::radians(pitch)) * Radius * 3.0f;
	camZ = sin(glm::radians(yaw)) * cos(glm::radians(pitch)) * Radius * 3.0f;

	cameraPos = glm::vec3(camX, camY, camZ);
}

int getPositionSize(const rapidjson::Value& value)
{
	int size = (value.Size() / 4) * 3;
	return size;
}

void createImageStore()
{

	//the "result" image
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WindowWidth, WindowHeight, 0, GL_RGBA, GL_FLOAT, NULL);

	glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
	glBindTexture(GL_TEXTURE_2D, texture);

}

int initializeLibraries()
{
	// Initialise GLFW
	if (!glfwInit())
	{
		fprintf(stderr, "Failed to initialize GLFW\n");
		getchar();
		return -1;
	}

	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // To make MacOS happy; should not be needed
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Open a window and create its OpenGL context
	window = glfwCreateWindow(WindowWidth, WindowHeight, "Assignment 8", NULL, NULL);
	if (window == NULL) {
		fprintf(stderr, "Failed to open GLFW window. If you have an Intel GPU, they are not 3.3 compatible. Try the 2.1 version of the tutorials.\n");
		getchar();
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// Initialize GLEW
	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		getchar();
		glfwTerminate();
		return -1;
	}
	return 1;
}

void draw()
{
	if (vao == 0)
	{
		float quadShaderVertices[] = {
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		// setup plane VAO
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadShaderVertices), &quadShaderVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	}
	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void setupCamera()
{
	float aspect = (float)WindowWidth / (float)WindowHeight;

	//positioning "eye"
	Radius = cameraPos.z;
	//look at point "target"
	cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f); //point to the origin of our scene

	//direction
	glm::vec3 cameraDirectionVec = glm::normalize(cameraPos - cameraTarget); //get the vector to point in the positive z 

	//specify the up vector
	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

	//right vector will be the cross product of our up vector and the direction vector
	glm::vec3 cameraRightVec = glm::normalize(glm::cross(up, cameraDirectionVec));

	//get the camera up vector, we have the right and direction vector, cross product will result in the up vec
	cameraUp = glm::cross(cameraDirectionVec, cameraRightVec);
	cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);

	projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
}

void readjustCamera()
{
	float radius = cameraPos.z;

	//rotation on the ZX plane (about Y axis)
	float deltaX = static_cast<float>(sin(glfwGetTime()) * radius);
	float deltaZ = static_cast<float>(cos(glfwGetTime()) * radius);

	//change eye and up accordingly
	cameraPos = glm::vec3(deltaX, 0.0f, deltaZ);

	//glm::vec3 cameraDirectionVec = glm::normalize(cameraPos - cameraTarget); //get the vector to point in the positive z 
}

std::string readShaderSource(const char* filePath) {
	std::ifstream file(filePath);
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}
