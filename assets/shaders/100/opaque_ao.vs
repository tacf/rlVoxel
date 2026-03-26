#version 100

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec4 vertexColor;

uniform mat4 mvp;
uniform float uGeometrySnapEnabled;
uniform vec2 uGeometrySnapResolution;

varying vec2 fragTexCoord;
varying vec4 fragColor;

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
