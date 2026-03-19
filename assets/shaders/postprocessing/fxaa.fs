#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 resolution;

out vec4 finalColor;

void main()
{
    vec2 texel = 1.0 / resolution;

    vec3 rgbNW = texture(texture0, fragTexCoord + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgbNE = texture(texture0, fragTexCoord + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgbSW = texture(texture0, fragTexCoord + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgbSE = texture(texture0, fragTexCoord + vec2( 1.0,  1.0) * texel).rgb;
    vec3 rgbM  = texture(texture0, fragTexCoord).rgb;

    vec3 lumaWeights = vec3(0.299, 0.587, 0.114);

    float lumaNW = dot(rgbNW, lumaWeights);
    float lumaNE = dot(rgbNE, lumaWeights);
    float lumaSW = dot(rgbSW, lumaWeights);
    float lumaSE = dot(rgbSE, lumaWeights);
    float lumaM  = dot(rgbM,  lumaWeights);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * (1.0 / 8.0)),
        1.0 / 128.0
    );

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * texel;

    vec3 rgbA = 0.5 * (
        texture(texture0, fragTexCoord + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(texture0, fragTexCoord + dir * (2.0 / 3.0 - 0.5)).rgb
    );

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(texture0, fragTexCoord + dir * -0.5).rgb +
        texture(texture0, fragTexCoord + dir *  0.5).rgb
    );

    float lumaB = dot(rgbB, lumaWeights);
    vec3 result = ((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB;

    float alpha = texture(texture0, fragTexCoord).a;
    finalColor = vec4(result, alpha) * colDiffuse * fragColor;
}