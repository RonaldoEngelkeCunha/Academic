#ifndef RASTER_SHADERS_STR_H
#define RASTER_SHADERS_STR_H

#include <iostream>

std::string vertShaderStr = R"(
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 texCoord;

void main() {
    texCoord = aTexCoord;
    gl_Position = vec4(aPos, 1.0); 
}

)";
std::string fragShaderStr = R"(
#version 430 core
in vec2 texCoord;

out vec4 fragColor;

uniform sampler2D myTexture;

void main() { 
	fragColor = vec4(texture(myTexture, texCoord)); 
}
)";
#endif