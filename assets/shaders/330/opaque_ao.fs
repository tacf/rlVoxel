#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// These can be tuned without the need for remeshing.
uniform float uAoMin;    // darkest corner brightness, e.g. 0.60
uniform float uAoCurve;  // contrast shaping, e.g. 1.15

out vec4 finalColor;

void main(void) {
    vec4 texel = texture(texture0, fragTexCoord);
    
    // fragColor.rgb already contains your skylight/face tint from mesher.c
    vec3 lit = texel.rgb * fragColor.rgb * colDiffuse.rgb;
    
    // fragColor.a is AO packed in the mesh as 0..1 brightness factor
    float ao = clamp(fragColor.a, 0.0, 1.0);
    ao = mix(uAoMin, 1.0, ao);
    ao = pow(ao, uAoCurve);
    
    finalColor = vec4(lit * ao, 1.0);
}
