Simón Gasión
simon.gasion01@estudiant.upf.edu
u186412
240126


Done:
- Alpha sorting:
renderables (didnt use structure seen in the lesson - done using the alredy existing Node class with slight modifications since it alredy contains the data needed)
alpha elements rendered last and sorted by distance

- Phong: 
ambient light + emissive + occlusion
point light
directional light
spot light (TODO)
ranged attenuation

- Several lights: 
single pass
multi pass (some inconsistencies in directional lights)
frustum culling (TODO, test if sphere overlaps with bounding boxes and camera BoundingBoxSphereOverlap)


(new) Render options:
- Multipass lights		
Render lights using single pass or multi pass implementation.
- Render with lights		
Activate lab 1 implementation.
- Enable normals
Account for normalmaps for rendering
- Enable occlusion
TODO
- Enable emissive
Accouns for emissive texture when rendering
- Enable specular
TODO