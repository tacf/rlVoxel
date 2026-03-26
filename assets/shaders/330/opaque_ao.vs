#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
//in vec3 vertexNormal;
in vec4 vertexColor;    // rgb = face lighting/tint, a = baked AO

uniform mat4 mvp;
uniform float uGeometrySnapEnabled;
uniform vec2 uGeometrySnapResolution;

out vec2 fragTexCoord;
out vec4 fragColor;

void main(void) {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    vec4 clipPos = mvp * vec4(vertexPosition, 1.0);
    if (uGeometrySnapEnabled > 0.5 && clipPos.w > 0.0) {
        vec2 snapRes = max(uGeometrySnapResolution, vec2(1.0));
        vec2 ndc = clipPos.xy / clipPos.w;
        vec2 snappedNdc = floor(ndc * snapRes + vec2(0.5)) / snapRes;
        clipPos.xy = snappedNdc * clipPos.w;
    }
    gl_Position = clipPos;
}
