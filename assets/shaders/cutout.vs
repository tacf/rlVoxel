#version 330 core

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

out vec2 fragTexCoord;
out vec4 fragColor;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
uniform float uGeometrySnapEnabled;
uniform vec2 uGeometrySnapResolution;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    vec4 clipPos = matProjection * matView * matModel * vec4(vertexPosition, 1.0);
    if (uGeometrySnapEnabled > 0.5 && clipPos.w > 0.0) {
        vec2 snapRes = max(uGeometrySnapResolution, vec2(1.0));
        vec2 ndc = clipPos.xy / clipPos.w;
        vec2 snappedNdc = floor(ndc * snapRes + vec2(0.5)) / snapRes;
        clipPos.xy = snappedNdc * clipPos.w;
    }
    gl_Position = clipPos;
}
