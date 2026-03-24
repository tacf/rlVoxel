#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main(void) {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 color = texel.rgb * fragColor.rgb * colDiffuse.rgb;
    float alpha = texel.a * fragColor.a * colDiffuse.a;
    finalColor = vec4(color, alpha);
}
