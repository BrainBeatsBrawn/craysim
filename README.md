# craysim

A library (of modules) for creating simulations with compound ray.

You incorporate this into your program by compiling the modules.

See [craysim_minimal](https://github.com/BrainBeatsBrawn/craysim_minimal), [antmin](https://github.com/BrainBeatsBrawn/antmin) and [antpov](https://github.com/BrainBeatsBrawn/antpov) for example C++ code and CMakeLists.txt files to copy. 
craysim_minimal and antmin are both github template repositories that you can use as a starting point for new projects.

To use the code here, your program will need to access modules from [sebsjames/mathplot](https://github.com/sebsjames/mathplot), [sebsjames/maths](https://github.com/sebsjames/maths) and [BrainBeatsBrawn/oces_viewer](https://github.com/BrainBeatsBrawn/oces_viewer) (with additional dependency [tinygltf](https://github.com/sebsjames/tinygltf)). Your program will also need to link to the BrainBeatsBrawn fork of [compound-ray](https://github.com/BrainBeatsBrawn/compound-ray)

Your CMakeLists.txt will need to FindOptix using the FindOptix.cmake file [from this repository](https://github.com/BrainBeatsBrawn/craysim/tree/main/cmake).
