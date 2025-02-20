#include <iostream>
#include <thread>
#include <iomanip>
#include <texture.h>
#include <plane.h>
#include <sphere.h>
#include <bbox.h>
#include <camera.h>
#include <utilities.h>
#include <base_tracer.h>
#include <vec3.h>
#include <triangle.h>
#include <bvh.h>
#include <octree.h>
#include <kdtree.h>
std::map<std::string, Texture> textures_map = load_textures("textures");
double render_time = 0.0, build_time = 0.0;
int WIDTH = 1920, HEIGHT = 1080;
int total_num_prims = 0;
int no_threads = std::thread::hardware_concurrency();
std::vector<std::string> models;
accel_struct tree_type = none;
extern uint64_t num_ray_prim_tests, num_ray_prim_isects;
Tree *tree;

Tree* create_accel_struct(std::vector<std::shared_ptr<Object>> &scene)
{	
	switch (tree_type)
	{
		//Parameters: scene, isect_cost, traverse_cost, b_e, min_prims
		case kd: return new KDtree(scene, 80, 1, 0.5, 1);
		//Parameters: scene, min_prims
		case bvh: return new BVH(scene, 1);
		case octree: 
		{
			BBOX bounds;
			for (const std::shared_ptr<Object> &prim : scene) 
			{
				BBOX b = prim->get_bbox();
				bounds = BBOX::union_bbox(bounds, b);
			}
			//Parameters: scene, world_bound, max_depth, min_prims
			return new Octree(scene, bounds, 8, 1);
		}
		case none: return nullptr;
	}
	
	return nullptr;
}

void start_thread(const unsigned start, const unsigned end, Color *image, Camera &camera, std::vector<std::shared_ptr<Object>> &scene, std::vector<Light> &lights, int id)
{
	for (unsigned i = start; i < end; i++) 
  	{
		int x = i % WIDTH;
		int y = i / WIDTH;
		double new_x = (2.0 * x) / WIDTH - 1.0;
		double new_y = (-2.0 * y) / HEIGHT + 1.0;
		Color* pixel = image + (x + y * WIDTH);
		std::shared_ptr<Ray> camera_ray = camera.create_camera_ray(new_x, new_y); 

		if (tree_type == none) *pixel = cast_ray(camera_ray, scene, lights);
		else if (tree->intersect_tree(camera_ray))
		{
			//simple shading code for structures
			Color color = camera_ray->hit_color;

			if (!camera_ray->tex.empty())
			{
				Texture tex_image;
				Vec3 tex = camera_ray->vt0 * (1 - camera_ray->u - camera_ray->v) + camera_ray->vt1 * camera_ray->u + camera_ray->vt2 * camera_ray->v;

				for (auto x : textures_map) 
				{
					if (camera_ray->tex == x.first) 
					{
						tex_image = x.second;
						break;
					}
				}
				int i = round(tex.x * tex_image.width);
				int j = round(tex.y * tex_image.height);

				color = Color(tex_image.data[(i + j * tex_image.width) * tex_image.channels]/255.,
				tex_image.data[(i + j * tex_image.width) * tex_image.channels + 1]/255., 
				tex_image.data[(i + j * tex_image.width) * tex_image.channels + 2]/255.);
			}

			Color ambient = color * 0.2;
			Vec3 point = camera_ray->get_intersection_point();
			Vec3 normal = camera_ray->fn;
			for (unsigned i = 0; i < lights.size(); i++)
			{
				Vec3 light_dir = lights[0].get_direction(point);
				double r2 = light_dir.dot_product(light_dir);
				light_dir = light_dir.normalize();

				*pixel += (ambient + color * lights[0].get_intensity() / (4 * M_PI * r2) * std::max(0.0, normal.dot_product(light_dir)));
			}
		}
	}
}

void render_scene(Camera &camera, std::vector<std::shared_ptr<Object>> &scene, std::vector<Light> &lights) 
{
	Color* image = new Color[WIDTH * HEIGHT];
	std::thread *thread_pool = new std::thread[no_threads];
	int resolution = WIDTH * HEIGHT;
	int block = resolution / no_threads;
	int remainder = resolution % no_threads;

	std::cout << "Rendering scene " << fflush(stdout);
	auto timeStart = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < no_threads - 1; i++) 
		thread_pool[i] = std::thread(start_thread, i * block, (i + 1) * block, image, std::ref(camera), std::ref(scene), std::ref(lights), i);

	start_thread((no_threads - 1) * block, (no_threads)*block + remainder, image, std::ref(camera), std::ref(scene), std::ref(lights), no_threads-1);

	for (int i = 0; i < no_threads - 1; i++) 
		thread_pool[i].join();

	auto timeEnd = std::chrono::high_resolution_clock::now();
	render_time = std::chrono::duration<double, std::milli>(timeEnd - timeStart).count();
	std::cout <<"\rRendering scene DONE                        : " << std::setprecision(2) << std::fixed << (render_time) / 1000 << " (sec)" << std::endl;
	save_image(WIDTH, HEIGHT, image);
}

void setup_scene()
{
	std::cout << "--------------------------------------------------------" << std::endl;
	std::cout << "Loading scene " << fflush(stdout);
	
	//Change camera here
	//Parameters: Position, Look-at, Up-direction, field-of-view angle (currently 50). 
	//Default camera: Vec3(0, 1, 5), Vec3(0, 0, 0), Vec3(0, 2, 5)
	//Room camera: Vec3(0, 5, 5), Vec3(0, 2, 0), Vec3(0, 6, 5)
	//Gallery camera: Vec3(0, 5, -10), Vec3(0, 0, 0), Vec3(0, 6, -10)
	Camera camera(Vec3(0, 1, 5), Vec3(0, 0, 0), Vec3(0, 2, 5), ((50 * 0.5) * M_PI / 180.0), (double)WIDTH/(double)HEIGHT);
	std::vector<std::shared_ptr<Object>> scene;
	std::vector<Light> lights;

	//Add spheres & planes here (only works with base tracer)
	if (tree_type == none)
	{
		//Parameters: (position, radius, color, material type (diffuse, reflective, phong))
		std::shared_ptr<Sphere> sphere = std::shared_ptr<Sphere>(new Sphere(Vec3(3,1,-5), 1, Color(1,0,0), diffuse));
		scene.push_back(sphere);
		std::shared_ptr<Sphere> sphere1 = std::shared_ptr<Sphere>(new Sphere(Vec3(0,1,-5), 1, Color(1,0,0), phong));
		scene.push_back(sphere1);

		//Parameters: (position, direction/normal, color, material type (diffuse, reflective, phong))
		std::shared_ptr<Plane> plane = std::shared_ptr<Plane>(new Plane(Vec3(0, -1, 0), Vec3(0, 1, 0), Color(0.03,0.83,0.95), diffuse));
		scene.push_back(plane);
	}

	//Add lights
	//Parameters: position, color, intensity
	Light light(Vec3(0.0, 5.0, 0.0), Color(1), 500);
	lights.push_back(light);

	for (unsigned i = 0; i < models.size(); i++)
		load_mesh(models[i], scene);

	std::cout <<"\rLoading scene DONE" << std::endl;

	if (tree_type != none)
	{
		std::cout << "Building tree " << fflush(stdout);
		auto timeStart = std::chrono::high_resolution_clock::now();
		tree = create_accel_struct(scene);
		auto timeEnd = std::chrono::high_resolution_clock::now();
		build_time = std::chrono::duration<double, std::milli>(timeEnd - timeStart).count();
		std::cout <<"\rBuilding tree DONE                          : " << std::setprecision(2) << std::fixed << (build_time) / 1000 << " (sec)" << std::endl;
	}

	total_num_prims = scene.size();
	render_scene(camera, scene, lights);
}

int main(int argc, char *argv[]) 
{
	if (argc > 1 && strcmp("kd", argv[1]) == 0) tree_type = kd;
	else if (argc > 1 && strcmp("bvh", argv[1]) == 0) tree_type = bvh;
	else if (argc > 1 && strcmp("octree", argv[1]) == 0) tree_type = octree;

	char *token = strtok(argv[2], "x"); 
	WIDTH = atoi(token);
	token = strtok(NULL, "x");
	HEIGHT = atoi(token);

	no_threads = std::min(atoi(argv[3]), no_threads);

	std::cout << "-------------------------------------------------------" << std::endl;
	std::cout << "Ray Tracer is running" << std::endl;
	std::cout << "Resolution: " << WIDTH << "x" << HEIGHT << std::endl;
	std::cout << "Threads: " << no_threads << std::endl;

	if (argc > 4)
		for (int i = 4; i < argc; i++)
			models.push_back(argv[i]);

	setup_scene();

	std::cout << "--------------------------------------------------------" << std::endl;
	std::cout << "Overall time                                : " << std::setprecision(2) << std::fixed << (render_time + build_time) / 1000 << " (sec)" << std::endl;
	std::cout << "Total number of primitives                  : " << total_num_prims << std::endl;
	std::cout << "Total number of primary rays                : " << WIDTH*HEIGHT << std::endl;
	std::cout << "Total number of ray-primitive tests         : " << num_ray_prim_tests << std::endl;
	std::cout << "Total number of ray-primitive intersections : " << num_ray_prim_isects << std::endl;
	std::cout << "--------------------------------------------------------" << std::endl;

	return 0;
}