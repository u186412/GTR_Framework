Simón Gasión
simon.gasion01@estudiant.upf.edu
u186412
240126


Done:
- Alpha sorting:
renderables (didnt use structure seen in the lesson - done using the alredy existing Node class with slight modifications since it alredy contains the data needed)
alpha elements rendered last and sorted by distance
- Phong: 
ambient light
point light
directional light
spot light (i assumed cone_info contains the minimum and maximum angles, don't know if thats what it's supposed to represent)

- Several lights: 
single pass
multi pass (TODO)
frustum culling (TODO, test if sphere overlaps with bounding boxes and camera BoundingBoxSphereOverlap)

Render options:
- Multipass lights		Render lights using single pass or multi pass implementation.
- Render with lights		Activate lighting.
