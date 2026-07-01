# craysim

A library (of modules) for creating simulations with compound ray.

You incorporate this into your program by compiling the modules.

See [craysim_minimal](https://github.com/BrainBeatsBrawn/craysim_minimal) and [antpov](https://github.com/BrainBeatsBrawn/antpov) for example usage and CMakeLists.txt files to copy.

To use the code here, your program will need to access modules from [sebsjames/mathplot](https://github.com/sebsjames/mathplot), [sebsjames/maths](https://github.com/sebsjames/maths) and [BrainBeatsBrawn/oces_viewer](https://github.com/BrainBeatsBrawn/oces_viewer) (with additional dependency [tinygltf](https://github.com/sebsjames/tinygltf)). Your program will also need to link to  Seb's fork of [compound-ray](https://github.com/BrainBeatsBrawn/compound-ray)

Your CMakeLists.txt will need to FindOptix using the FindOptix.cmake file here.
