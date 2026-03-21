#version 100

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform float uAoMin;
uniform float uAoCurve;

void main(void) {
    vec4 texel = texture2D(texture0, fragTexCoord);

    vec3 lit = texel.rgb * fragColor.rgb * colDiffuse.rgb;

    float ao = clamp(fragColor.a, 0.0, 1.0);
    ao = mix(uAoMin, 1.0, ao);
    ao = pow(ao, uAoCurve);

    gl_FragColor = vec4(lit * ao, 1.0);
}
