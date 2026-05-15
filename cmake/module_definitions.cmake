#
# Define variables of module groups for use by client projects.
#
macro(setup_module_variables_for_craysim_maths maths_directory json_directory)
  set(CRAYSIM_MATHS_MODULES
    ${maths_directory}/sm/hdfdata.cppm
    ${maths_directory}/sm/config.cppm
    ${maths_directory}/sm/spline.cppm
    ${maths_directory}/sm/random_walk.cppm
    ${json_directory}/src/modules/json.cppm
  )
  set(CRAYSIM_MATHS_DOUBLEHEX_MODULES
    ${maths_directory}/sm/hdfdata.cppm
    ${maths_directory}/sm/config.cppm
    ${json_directory}/src/modules/json.cppm
    ${maths_directory}/sm/binomial.cppm
    ${maths_directory}/sm/nm_simplex.cppm
    ${maths_directory}/sm/bezcoord.cppm
    ${maths_directory}/sm/bezcurve.cppm
    ${maths_directory}/sm/bezcurvepath.cppm
    ${maths_directory}/sm/hex.cppm
    ${maths_directory}/sm/hexgrid.cppm
    ${maths_directory}/sm/hexgrid_hdf.cppm
  )
  set(CRAYSIM_MATHS_ANTBODY_MODULES
    ${maths_directory}/sm/config.cppm
    ${json_directory}/src/modules/json.cppm
  )
endmacro()

macro(setup_module_variables_for_craysim_mathplot base_directory)
  set(CRAYSIM_MATHPLOT_MODULES
    ${base_directory}/mplot/fps/profiler.cppm
    ${base_directory}/mplot/compoundray/interop.cppm
    ${base_directory}/mplot/compoundray/Ommatidium.cppm
    ${base_directory}/mplot/compoundray/EyeVisual.cppm
    ${base_directory}/mplot/VerticesVisual.cppm
    ${base_directory}/mplot/NormalsVisual.cppm
    ${base_directory}/mplot/InstancedScatterVisual.cppm
  )
  set(CRAYSIM_MATHPLOT_DOUBLEHEX_MODULES
    ${base_directory}/mplot/ScatterVisual.cppm
    ${base_directory}/mplot/QuiverVisual.cppm
    ${base_directory}/mplot/HexGridVisual.cppm
    ${base_directory}/mplot/LengthscaleVisual.cppm
  )
endmacro()

macro(setup_module_variables_for_craysim base_directory)
  set(CRAYSIM_MODULES
    ${base_directory}/modules/craysim_visual.cppm
  )
  set(CRAYSIM_DOUBLEHEX_MODULES
    ${base_directory}/modules/craysim_doublehexgrid.cppm
  )
  set(CRAYSIM_ANTBODY_MODULES
    ${base_directory}/modules/craysim_antbody.cppm
  )
endmacro()
