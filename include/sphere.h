// Theory and Implementation inspired by
// https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes
#ifndef SPHERE_H
#define SPHERE_H
#include <object.h>

class Sphere : public Object
{
private:
	double radius;
	Vec3 position;
	
public:
	Sphere();
	Sphere(Vec3 position, double radius, Color color, Material material);
	double get_radius();
	bool intersected(std::shared_ptr<Ray> ray, int index) override;
	Vec3 get_normal(Vec3 point) override;
	BBOX get_bbox() override;
};

#endif