#include "renderer.h"

#include <algorithm> //sort

#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"

#include "scene.h"

using namespace SCN;

//some globals
GFX::Mesh sphere;

//struct sRenderable {
//	Material* material;
//	GFX::Mesh* mesh;
//	mat4 model;
//	float distanceToCamera;
//};

std::vector<SCN::Node*> default_objects;
std::vector<SCN::Node*> semitransparent_objects;
std::vector<LightEntity*> lights;

bool compareDist(Node* s1, Node* s2) { 
	return s1->distance_to_camera > s2->distance_to_camera;
}

Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	scene = nullptr;
	skybox_cubemap = nullptr;
	use_multipass = false;
	render_lights = true;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();
}

void Renderer::setupScene()
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;
}

void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();
	//clear lights and semitransparent nodes
	lights.clear();
	semitransparent_objects.clear();
	default_objects.clear();

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if(skybox_cubemap)
		renderSkybox(skybox_cubemap);

	//pass 1: store entities in containers
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible )
			continue;

		//categorize all objects into containers
		if (ent->getType() == eEntityType::PREFAB) //prefabs
		{
			PrefabEntity* pent = (SCN::PrefabEntity*)ent;
			if (pent->prefab)
				categorizeNodes(&pent->root, camera);
		}
		else if (ent->getType() == eEntityType::LIGHT) { //light objects
			//IDEA: test sphere in frustum to cull invisible point (+spot) lights
			//IDEA: test spheres to bounding boxes and cull invisible lights
			//TO-DO: pasar primero antes de render!! otro bucle!
			//downcast to EntityLight and store in light array
			LightEntity* light = (SCN::LightEntity*)ent; 
			lights.push_back(light);
		}
	}
	//pass 2: render entities
	for (int i = 0; i < default_objects.size(); i++)
	{
		renderNode(default_objects[i], camera);
	}
	//render semitransparent entities
	//sort blending vector - sorts nodes by distance in descending order
	std::sort(std::begin(semitransparent_objects), std::end(semitransparent_objects), compareDist);
	for (int i = 0; i < semitransparent_objects.size(); i++)
	{
		renderNode(semitransparent_objects[i], camera);
	}
}


void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);
	cameraToShader(camera, shader);
	shader->setUniform("u_texture", cubemap, 0);
	sphere.render(GL_TRIANGLES);
	shader->disable();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

//renders a node of the prefab and its children
void Renderer::renderNode(SCN::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true);

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			if(render_boundaries)
				node->mesh->renderBounding(node_model, true);
			render_lights ? renderMeshWithMaterialLights(node_model, node->mesh, node->material) : renderMeshWithMaterial(node_model, node->mesh, node->material);
		}
	}
}

void Renderer::categorizeNodes(SCN::Node* node, Camera* camera) { //adds node and children nodes to their respective container

	if (node->material && node->material->alpha_mode == SCN::eAlphaMode::BLEND) { //objects with transparency
		Matrix44 global_model = node->getGlobalMatrix(); //'fast' option does not work - global_model not set correctly?
		float dist = std::sqrt(std::pow(global_model.getTranslation().x - camera->eye.x, 2) + std::pow(global_model.getTranslation().y - camera->eye.y, 2) + std::pow(global_model.getTranslation().z - camera->eye.z, 2));
		node->distance_to_camera = dist;
		semitransparent_objects.push_back(node);
	}
	else //other objects
	{
		//distance is only really used for semitransparent nodes, ignore here to optimize resources
		/*Matrix44 global_model = node->getGlobalMatrix();
		float dist = std::sqrt(std::pow(global_model.getTranslation().x - camera->eye.x, 2) + std::pow(global_model.getTranslation().y - camera->eye.y, 2) + std::pow(global_model.getTranslation().z - camera->eye.z, 2));
		node->distance_to_camera = dist; */
		default_objects.push_back(node);
	}
	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i) {
		categorizeNodes(node->children[i], camera);
	}
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	GFX::Texture* texture = NULL;
	Camera* camera = Camera::current;
	
	texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = GFX::Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	//lab1: send to global renderable vector, postpone rendering
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		glDisable(GL_BLEND);
	}
	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);


    assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("texture");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	if(texture)
		shader->setUniform("u_texture", texture, 0);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	if (render_wireframe)
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}


void Renderer::renderMeshWithMaterialLights(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	GFX::Texture* texture = NULL;
	Camera* camera = Camera::current;

	texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	//TO-DO implement all this, check ppt
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = GFX::Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		glDisable(GL_BLEND);
	}
	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);


	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("lightSP"); //TO-DO: sp or mp

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	lightToShaderSP(shader); //TO-DO: multiple lights
	float t = getTime();
	shader->setUniform("u_time", t);
	shader->setUniform("u_ambient_light", scene->ambient_light);

	shader->setUniform("u_color", material->color);
	if (texture)
		shader->setUniform("u_texture", texture, 0);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void SCN::Renderer::cameraToShader(Camera* camera, GFX::Shader* shader)
{
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix );
	shader->setUniform("u_camera_position", camera->eye);
}

void SCN::Renderer::lightToShaderSP(GFX::Shader* shader) {
	Vector3f light_positions[MAX_LIGHTS_SP];
	Vector3f light_fronts[MAX_LIGHTS_SP];
	Vector3f light_colors[MAX_LIGHTS_SP];
	Vector2f cones_info[MAX_LIGHTS_SP];
	float max_distances[MAX_LIGHTS_SP];
	int light_types[MAX_LIGHTS_SP];
	int num_lights = lights.size();
	for (int i = 0; i < num_lights; i++) {
		light_positions[i] = lights[i]->root.model.getTranslation();
		light_fronts[i] = lights[i]->root.model.frontVector().normalize();
		light_colors[i] = lights[i]->color * lights[i]->intensity;
		cones_info[i] = lights[i]->cone_info;
		max_distances[i] = lights[i]->max_distance;
		light_types[i] = (int)lights[i]->light_type;
	}
	shader->setUniform("u_num_lights", num_lights);
	shader->setUniform3Array("u_light_pos", (float*)&light_positions, MAX_LIGHTS_SP);
	shader->setUniform3Array("u_light_front", (float*)&light_fronts, MAX_LIGHTS_SP);
	shader->setUniform3Array("u_light_col", (float*)&light_colors, MAX_LIGHTS_SP);
	shader->setUniform1Array("u_max_distance", (float*)&max_distances, MAX_LIGHTS_SP);
	shader->setUniform1Array("u_light_type", (int*)&light_types, MAX_LIGHTS_SP);
}

#ifndef SKIP_IMGUI

void Renderer::showUI()
{
		
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);
	ImGui::Checkbox("Multipass lights", &use_multipass);
	ImGui::Checkbox("Render with lights", &render_lights);

	//add here your stuff
	//...
}

#else
void Renderer::showUI() {}
#endif