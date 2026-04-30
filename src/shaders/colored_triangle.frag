#version 450

//shader input
layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inWorldPosition;

//output write
layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec3 normal = normalize(inNormal);
	vec3 baseColor = max(inColor, vec3(0.7));

	vec3 lightDirection = normalize(vec3(0.45, -0.75, 0.6));
	float diffuse = max(dot(normal, -lightDirection), 0.0);

	vec3 viewDirection = normalize(vec3(0.0, 0.0, 5.0) - inWorldPosition);
	float rim = pow(1.0 - max(dot(normal, viewDirection), 0.0), 2.0);

	vec3 ambient = vec3(0.18, 0.20, 0.24);
	vec3 litColor = baseColor * (ambient + diffuse * vec3(0.95, 0.88, 0.75));
	litColor += rim * vec3(0.18, 0.28, 0.45);

	outFragColor = vec4(litColor, 1.0f);
}
