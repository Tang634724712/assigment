﻿#include <iostream>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl2.h>

using namespace glm;

GLFWwindow* window = nullptr;
int err_no;

int check_gl_err()
{
	while ((err_no = glGetError()) != GL_NO_ERROR)
		printf("%d: %s\n", err_no, glewGetErrorString(err_no));
	return err_no;
}

GLuint load_texture(const char* tex_file)
{
	int img_width, img_height, img_channel;
	auto img_data = stbi_load(tex_file, &img_width, &img_height, &img_channel, 4);
	if (!img_data)
	{
		printf("cannot load texture\n");
		return false;
	}
	GLuint ret = 0;
	glGenTextures(1, &ret);
	glBindTexture(GL_TEXTURE_2D, ret);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img_width, img_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glBindTexture(GL_TEXTURE_2D, 0);
	return ret;
}

struct Model
{
	std::vector<vec3> vertices;
	std::vector<vec3> normals;
	std::vector<vec2> uvs;
	std::vector<uint> indices;
	GLuint texture = 0;

	bool load(const char* obj_file, const char* tex_file)
	{
		Assimp::Importer importer;
		auto load_flags =
			aiProcess_RemoveRedundantMaterials |
			aiProcess_FlipUVs;
		auto scene = importer.ReadFile(obj_file, load_flags);
		if (!scene)
		{
			printf("cannot open model: %s\n", importer.GetErrorString());
			return false;
		}

		for (auto i = 0; i < scene->mNumMeshes; i++)
		{
			auto src = scene->mMeshes[i];
			for (auto j = 0; j < src->mNumVertices; j++)
			{
				vertices.push_back(*(vec3*)&src->mVertices[j]);
				normals.push_back(src->mNormals ? *(vec3*)&src->mNormals[j] : vec3(0.f));
				uvs.push_back(src->mTextureCoords[0] ? *(vec2*)&src->mTextureCoords[0][j] : vec2(0.f));
			}
			for (auto j = 0; j < src->mNumFaces; j++)
			{
				indices.push_back(src->mFaces[j].mIndices[0]);
				indices.push_back(src->mFaces[j].mIndices[1]);
				indices.push_back(src->mFaces[j].mIndices[2]);
			}
		}

		if (tex_file)
		{
			texture = load_texture(tex_file);
			if (!texture)
			{
				printf("cannot load texture\n");
				return false;
			}
		}

		return true;
	}

	void draw()
	{
		glBindTexture(GL_TEXTURE_2D, texture);
		glBegin(GL_TRIANGLES);
		for (auto i : indices)
		{
			auto& uv = uvs[i];
			glTexCoord2f(uv.x, uv.y);
			auto& nor = normals[i];
			glNormal3f(nor.x, nor.y, nor.z);
			auto& v = vertices[i];
			glVertex3f(v.x, v.y, v.z);
		}
		glEnd();
	}
}cow;

GLuint create_shader(GLuint type, const std::string& source)
{
	auto ret = glCreateShader(type);
	const char* sources[] = {
		source.c_str()
	};
	int lengths[] = {
		source.size()
	};
	glShaderSource(ret, 1, sources, lengths);
	glCompileShader(ret);
	int ok;
	glGetShaderiv(ret, GL_COMPILE_STATUS, &ok);
	if (ok == GL_FALSE)
	{
		int len;
		glGetShaderiv(ret, GL_INFO_LOG_LENGTH, &len);
		std::string str;
		str.resize(len);
		glGetShaderInfoLog(ret, str.size(), &len, (char*)str.data());
		printf("%s\n", str.c_str());
		assert(0);
	}
	return ret;
}

GLuint create_program(GLuint vertex_shader, GLuint fragment_shader)
{
	auto ret = glCreateProgram();
	glAttachShader(ret, vertex_shader);
	glAttachShader(ret, fragment_shader);
	glLinkProgram(ret);
	int ok;
	glGetProgramiv(ret, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE)
	{
		int len;
		glGetProgramiv(ret, GL_INFO_LOG_LENGTH, &len);
		std::string str;
		str.resize(len);
		glGetProgramInfoLog(ret, str.size(), &len, (char*)str.data());
		printf("%s\n", str.c_str());
		assert(0);
	}
	return ret;
}

struct Camera
{
	vec3 coord = vec3(0.f, 1.f, 0.f);
	float x_angle = 0.f;
	float y_angle = 0.f;
	vec3 forward_dir;
	vec3 side_dir;
	bool forward = false;
	bool backward = false;
	bool left = false;
	bool right = false;

	mat4 update()
	{
		mat4 rot;
		rot = rotate(mat4(1.f), radians(x_angle), vec3(0.f, 1.f, 0.f));
		rot = rotate(rot,		radians(y_angle), vec3(1.f, 0.f, 0.f));
		forward_dir = -rot[2];
		side_dir = rot[0];

		if (forward)
			coord += forward_dir * 0.1f;
		if (backward)
			coord -= forward_dir * 0.1f;
		if (left)
			coord -= side_dir * 0.1f;
		if (right)
			coord += side_dir * 0.1f;

		return inverse(translate(mat4(1.f), coord) * rot);
	}
}camera;

template<class T, size_t N>
constexpr size_t size(T(&)[N]) { return N; }

void prism(float depth, float width, float height, float y_off = 0.f)
{
	auto hf_width = width * 0.5f;

	glNormal3f(0.f, 0.f, 1.f);
	glTexCoord2f(0.f, 0.f);
	glVertex3f(0.f, 0.f + y_off, 0.f);
	glTexCoord2f(1.f, 0.f);
	glVertex3f(width, 0.f + y_off, 0.f);
	glTexCoord2f(0.f, 1.f);
	glVertex3f(hf_width, height + y_off, 0.f);

	auto slop_normal1 = normalize(cross(vec3(0.f, 0.f, 1.f), vec3(hf_width, -height, 0.f)));
	glNormal3f(slop_normal1.x, slop_normal1.y, slop_normal1.z);
	glTexCoord2f(0.f, 0.f);
	glVertex3f(hf_width, height + y_off, 0.f);
	glTexCoord2f(0.f, 1.f);
	glVertex3f(width, 0.f + y_off, 0.f);
	glTexCoord2f(1.f, 0.f);
	glVertex3f(hf_width, height + y_off, -depth);
	glTexCoord2f(1.f, 0.f);
	glVertex3f(hf_width, height + y_off, -depth);
	glTexCoord2f(0.f, 1.f);
	glVertex3f(width, 0.f + y_off, 0.f);
	glTexCoord2f(1.f, 1.f);
	glVertex3f(width, 0.f + y_off, -depth);

	auto slop_normal2 = normalize(cross(vec3(0.f, 0.f, -1.f), vec3(-hf_width, -height, 0.f)));
	glNormal3f(slop_normal2.x, slop_normal2.y, slop_normal2.z);
	glTexCoord2f(0.f, 0.f);
	glVertex3f(hf_width, height + y_off, -depth);
	glTexCoord2f(0.f, 1.f);
	glVertex3f(0.f, 0.f + y_off, -depth);
	glTexCoord2f(1.f, 0.f);
	glVertex3f(hf_width, height + y_off, 0.f);
	glTexCoord2f(1.f, 0.f);
	glVertex3f(hf_width, height + y_off, 0.f);
	glTexCoord2f(0.f, 1.f);
	glVertex3f(0.f, 0.f + y_off, -depth);
	glTexCoord2f(1.f, 1.f);
	glVertex3f(0.f, 0.f + y_off, 0.f);

	glNormal3f(0.f, 0.f, -1.f);
	glTexCoord2f(0.f, 0.f);
	glVertex3f(0.f, 0.f + y_off, -depth);
	glTexCoord2f(0.f, 1.f);
	glVertex3f(hf_width, height + y_off, -depth);
	glTexCoord2f(1.f, 0.f);
	glVertex3f(width, 0.f + y_off, -depth);
}

auto move = 0; // stand, forward, backward

static GLFWkeyfun prev_keyfun = nullptr;
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (prev_keyfun)
		prev_keyfun(window, key, scancode, action, mods);
	if (ImGui::GetIO().WantCaptureKeyboard)
		return;
	if (key == GLFW_KEY_W)
	{
		if (action == GLFW_PRESS)
			camera.forward = true;
		else if (action == GLFW_RELEASE)
			camera.forward = false;
	}
	else if (key == GLFW_KEY_S)
	{
		if (action == GLFW_PRESS)
			camera.backward = true;
		else if (action == GLFW_RELEASE)
			camera.backward = false;
	}
	else if (key == GLFW_KEY_A)
	{
		if (action == GLFW_PRESS)
			camera.left = true;
		else if (action == GLFW_RELEASE)
			camera.left = false;
	}
	else if (key == GLFW_KEY_D)
	{
		if (action == GLFW_PRESS)
			camera.right = true;
		else if (action == GLFW_RELEASE)
			camera.right = false;
	}
	else if (key == GLFW_KEY_SPACE)
	{
		if (action == GLFW_PRESS && move == 0)
			move = 1;
	}
}

bool dragging = false;
double last_x = 0;
double last_y = 0;

static GLFWmousebuttonfun prev_mousebuttonfun = nullptr;
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (prev_mousebuttonfun)
		prev_mousebuttonfun(window, button, action, mods);
	if (ImGui::GetIO().WantCaptureMouse)
		return;
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			glfwGetCursorPos(window, &last_x, &last_y);
			dragging = true;
		}
		else if (action == GLFW_RELEASE)
			dragging = false;
	}
}

static GLFWcursorposfun prev_cursorposfun = nullptr;
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (prev_cursorposfun)
		prev_cursorposfun(window, xpos, ypos);
	if (ImGui::GetIO().WantCaptureMouse)
		return;
	if (dragging)
	{
		camera.x_angle -= xpos - last_x;
		camera.y_angle -= ypos - last_y;
		last_x = xpos;
		last_y = ypos;
	}
}

const auto GRIDX = 20U;
const auto GRIDY = 40U;
const auto GRIDS = 0.2f;

int main()
{
	if (!glfwInit())
		return 0;

	window = glfwCreateWindow(800, 600, "", nullptr, nullptr);
	if (!window)
		return 0;

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	if (glewInit() != GLEW_OK)
	{
		printf("glew init failed\n");
		return 0;
	}

	if (!cow.load("cow.obj", nullptr))
		return 0;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL2_Init();

	prev_keyfun = glfwSetKeyCallback(window, key_callback);
	prev_mousebuttonfun = glfwSetMouseButtonCallback(window, mouse_button_callback);
	prev_cursorposfun = glfwSetCursorPosCallback(window, cursor_position_callback);

	auto grid_program = create_program(
		create_shader(GL_VERTEX_SHADER, 
		"#version 120\n"
		"void main() {\n"
		"	gl_FrontColor = gl_Color;\n"
		"	gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;\n"
		"}"), 
		create_shader(GL_FRAGMENT_SHADER, 
		"#version 120\n"
		"void main() {\n"
		"	gl_FragColor = gl_Color;\n"
		"}"));
	auto object_program = create_program(
		create_shader(GL_VERTEX_SHADER,
			"#version 130\n"
			"uniform mat4 proj_mat;\n"
			"uniform mat4 view_mat;\n"
			"uniform mat4 model_mat;\n"
			"uniform mat3 normal_mat;\n"
			"uniform vec3 camera_coord;\n"
			"varying vec2 uv;\n"
			"varying vec3 normal;\n"
			"varying vec3 coord;\n"
			"varying vec3 view;\n"
			"void main() {\n"
			"	uv = gl_MultiTexCoord0.xy;\n"
			"	normal = normalize(normal_mat * gl_Normal);\n"
			"	coord = vec3(model_mat * gl_Vertex);\n"
			"	view = normalize(coord - camera_coord);\n"
			"	gl_Position = proj_mat * view_mat * model_mat * gl_Vertex;\n"
			"}"),
		create_shader(GL_FRAGMENT_SHADER,
			"#version 130\n"
			"uniform sampler2D tex;\n"
			"uniform vec3 point_light1;\n"
			"uniform vec3 point_light2;\n"
			"varying vec2 uv;\n"
			"varying vec3 normal;\n"
			"varying vec3 coord;\n"
			"varying vec3 view;\n"
			"vec3 lighting(vec3 L, vec3 N, vec3 V, vec3 color, vec3 albedo) {\n"
			"	vec3 R = reflect(L, N);\n"
			"	float nl = max(0, dot(N, L));\n"
			"	vec3 diff = albedo * nl * 0.5;\n"
			"	float spec = pow(max(dot(R, V), 0.0), 8.0) * 0.5;\n"
			"	return diff + vec3(spec);\n"
			"}\n"
			"vec3 point_lighting(vec3 p, vec3 albedo) {\n"
			"	vec3 L = p - coord;\n"
			"	float d = length(L);\n"
			"	float a = 10000.0 / (d * d);\n"
			"	L = normalize(L);\n"
			"	return lighting(L, normal, view, vec3(1.0, 0.77, 0.56) * a, albedo);\n"
			"}\n"
			"void main() {\n"
			"	vec3 albedo = texture(tex, uv).rgb;\n"
			"	vec3 color = vec3(0.0);\n"
			"	color += albedo * vec3(0.788, 0.88, 1.0) * 0.2; // ambient\n"
			"	color += lighting(vec3(0, 1, 0), normal, view, vec3(0.788, 0.88, 1.0), albedo); // directional light\n"
			"	color += point_lighting(point_light1, albedo); // point light1\n"
			"	color += point_lighting(point_light2, albedo); // point light2\n"
			"	gl_FragColor = vec4(color, 1.0);\n"
			"}"));
	auto proj_mat_id = glGetUniformLocation(object_program, "proj_mat");
	auto view_mat_id = glGetUniformLocation(object_program, "view_mat");
	auto model_mat_id = glGetUniformLocation(object_program, "model_mat");
	auto normal_mat_id = glGetUniformLocation(object_program, "normal_mat");
	auto camera_coord_id = glGetUniformLocation(object_program, "camera_coord");
	auto light1_id = glGetUniformLocation(object_program, "point_light1");
	auto light2_id = glGetUniformLocation(object_program, "point_light2");

	auto quadrics = gluNewQuadric();
	gluQuadricTexture(quadrics, GL_TRUE);
	gluQuadricNormals(quadrics, GLU_SMOOTH);

	auto body_texture = load_texture("scrap.jpg");
	auto wheel_texture = load_texture("wheels.jpg");

	auto speed = (GRIDY * GRIDS - 1.5f) / 10.f / 60.f;
	auto train_z = GRIDY * 0.5f * GRIDS;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		//ImGui_ImplOpenGL2_NewFrame();
		//ImGui_ImplGlfw_NewFrame();
		//ImGui::NewFrame();

		int win_width;
		int win_height;
		glfwGetWindowSize(window, &win_width, &win_height);
		glViewport(0, 0, win_width, win_height);

		glClearColor(0.7f, 0.7f, 0.7f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_TEXTURE_2D);
		glEnable(GL_DEPTH);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_NORMALIZE);

		auto proj = perspective(radians(45.f), (float)win_width / (float)win_height, 1.f, 1000.f);
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(&proj[0][0]);
		glMatrixMode(GL_MODELVIEW);
		auto view = camera.update();
		auto mv = view * mat4(1.f);
		glLoadMatrixf(&mv[0][0]);

		glUseProgram(grid_program);

		glBegin(GL_LINES);
		auto offset = vec2(GRIDX, GRIDY) * GRIDS * -0.5f;
		for (auto y = 0; y < GRIDY + 1; y++)
		{
			glColor3f(0.78f, 0.88f, 0.80f);
			glVertex3f(0.f + offset.x, 0.f, y * GRIDS + offset.y);
			glVertex3f(GRIDX * GRIDS + offset.x, 0.f, y * GRIDS + offset.y);
		}
		for (auto x = 0; x < GRIDX + 1; x++)
		{
			if (x == 8 || x == 9)
				glColor3f(0.f, 0.f, 0.f);
			else
				glColor3f(0.78f, 0.88f, 0.80f);
			glVertex3f(x * GRIDS + offset.x, 0.f, 0.f + offset.y);
			glVertex3f(x * GRIDS + offset.x, 0.f, GRIDY * GRIDS + offset.y);
		}
		glEnd();

		if (move == 1)
		{
			train_z -= speed;
			if (train_z <= GRIDY * -0.5f * GRIDS + 1.5f)
				move = 2;
		}
		else if (move == 2)
		{
			train_z += speed;
			if (train_z >= GRIDY * 0.5f * GRIDS)
				move = 1;
		}

		auto train_pos = vec3((GRIDX * -0.5f + 8.f) * GRIDS - 0.15f, 0.3f, train_z);

		glUseProgram(object_program);
		glUniformMatrix4fv(proj_mat_id, 1, false, &proj[0][0]);
		glUniformMatrix4fv(view_mat_id, 1, false, &view[0][0]);
		glUniform3fv(camera_coord_id, 1, &camera.coord[0]);
		{
			auto p = train_pos + vec3(0.15, 0.55, -1.6);
			glUniform3fv(light1_id, 1, &p[0]);
		}
		{
			auto p = train_pos + vec3(0.35, 0.55, -1.6);
			glUniform3fv(light2_id, 1, &p[0]);
		}

		auto train_transform = translate(mat4(1.f), train_pos);
		{
			glUniformMatrix4fv(model_mat_id, 1, false, &train_transform[0][0]);
			auto nor = transpose(inverse(mat3(train_transform)));
			glUniformMatrix3fv(normal_mat_id, 1, false, &nor[0][0]);
		}
		glBindTexture(GL_TEXTURE_2D, body_texture);
		glBegin(GL_TRIANGLES);
		prism(1.5f, 0.5f, 0.5f);
		prism(0.5f, 0.5f, 0.25f, 0.5f);
		glEnd();

		glBindTexture(GL_TEXTURE_2D, wheel_texture);
		auto draw_wheel = [&](const vec3& pos) {
			auto wheel_transform = train_transform * translate(mat4(1.f), pos) * rotate(mat4(1.f), radians(90.f), vec3(0.f, 1.f, 0.f));
			if (move != 0)
			{
				static auto ang = 0.f;
				wheel_transform = wheel_transform * rotate(mat4(1.f), radians(ang), vec3(0.f, 0.f, 1.f));
				ang += move == 1 ? -12.f : 12.f;
			}
			{
				auto m = wheel_transform * rotate(mat4(1.f), radians(180.f), vec3(0.f, 1.f, 0.f));
				glUniformMatrix4fv(model_mat_id, 1, false, &m[0][0]);
				auto nor = transpose(inverse(mat3(m)));
				glUniformMatrix3fv(normal_mat_id, 1, false, &nor[0][0]);
			}
			gluDisk(quadrics, 0.f, 0.3f, 16, 16);
			{
				auto m = wheel_transform * translate(mat4(1.f), vec3(0.f, 0.f, 0.1f));
				glUniformMatrix4fv(model_mat_id, 1, false, &m[0][0]);
				auto nor = transpose(inverse(mat3(m)));
				glUniformMatrix3fv(normal_mat_id, 1, false, &nor[0][0]);
			}
			gluDisk(quadrics, 0.f, 0.3f, 16, 16);
			{
				glUniformMatrix4fv(model_mat_id, 1, false, &wheel_transform[0][0]);
				auto nor = transpose(inverse(mat3(wheel_transform)));
				glUniformMatrix3fv(normal_mat_id, 1, false, &nor[0][0]);
			}
			gluCylinder(quadrics, 0.3f, 0.3f, 0.1f, 16, 16);
		};
		draw_wheel(vec3(-0.1f, 0.f, 0.f));
		draw_wheel(vec3(-0.1f, 0.f, -0.6f));
		draw_wheel(vec3(-0.1f, 0.f, -1.2f));
		draw_wheel(vec3(0.5f, 0.f, 0.f));
		draw_wheel(vec3(0.5f, 0.f, -0.6f));
		draw_wheel(vec3(0.5f, 0.f, -1.2f));

		{
			auto m = scale(mat4(1.f), vec3(0.1f));
			m = rotate(m, radians(-90.f), vec3(1.f, 0.f, 0.f));
			m = translate(m, vec3((GRIDX * -0.5f + 8.f) * GRIDS, 0.f, (GRIDY * 0.5f) * GRIDS));
			glUniformMatrix4fv(model_mat_id, 1, false, &m[0][0]);
			auto nor = transpose(inverse(mat3(m)));
			glUniformMatrix3fv(normal_mat_id, 1, false, &nor[0][0]);
		}
		cow.draw();

		//ImGui::Render();
		//ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}
