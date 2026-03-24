#version 100

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

void main(void) {
    vec4 texel = texture2D(texture0, fragTexCoord);
    vec3 color = texel.rgb * fragColor.rgb * colDiffuse.rgb;
    float alpha = texel.a * fragColor.a * colDiffuse.a;
    gl_FragColor = vec4(color, alpha);
}
