#version 330 core

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

out vec2 fragTexCoord;
out vec4 fragColor;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = matProjection * matView * matModel * vec4(vertexPosition, 1.0);
}
