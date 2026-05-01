#
# Define variables of module groups for use by client projects.
#
macro(setup_module_variables_for_craysim_maths base_directory)
  set(CRAYSIM_MATHS_MODULES
    ${base_directory}/sm/hdfdata.cppm
  )
  set(CRAYSIM_MATHS_DOUBLEHEX_MODULES
    ${base_directory}/sm/hdfdata.cppm
    ${base_directory}/sm/binomial.cppm
    ${base_directory}/sm/nm_simplex.cppm
    ${base_directory}/sm/bezcoord.cppm
    ${base_directory}/sm/bezcurve.cppm
    ${base_directory}/sm/bezcurvepath.cppm
    ${base_directory}/sm/hex.cppm
    ${base_directory}/sm/hexgrid.cppm
    ${base_directory}/sm/hexgrid_hdf.cppm
  )
  set(CRAYSIM_MATHS_ANTBODY_MODULES
    ${base_directory}/sm/config.cppm
    ${base_directory}/json/src/modules/json.cppm
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
    ${base_directory}/modules/tk_spline.cppm           # GPL v2
    ${base_directory}/modules/craysim_random_walk.cppm # Uses tk_spline, GPL v2
    ${base_directory}/modules/craysim_visual.cppm      # Uses random_walk, GPL v2
  )
  set(CRAYSIM_DOUBLEHEX_MODULES
    ${base_directory}/modules/craysim_doublehexgrid.cppm
  )
  set(CRAYSIM_ANTBODY_MODULES
    ${base_directory}/modules/craysim_antbody.cppm
  )
endmacro()
