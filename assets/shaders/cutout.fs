#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    vec4 vertexColor = fragColor;
    
    // Mix texture color with vertex color (tint)
    vec4 finalTexColor = vec4(texColor.rgb * vertexColor.rgb, texColor.a * vertexColor.a);
    
    // Alpha test - discard pixels below threshold
    if (finalTexColor.a < 0.1) {
        discard;
    }
    
    finalColor = finalTexColor;
}
