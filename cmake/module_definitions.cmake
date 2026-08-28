#
# Define variables of module groups for use by client projects.
#
macro(setup_module_variables_for_craysim craysim_directory mathplot_directory maths_directory json_directory)

  # load the macro 'setup_module_variables_for_maths' from module_definitions.cmake (found in the maths_directory)
  include(${maths_directory}/cmake/module_definitions.cmake)
  # Use the macro to load the SM_*_MODULES variables
  setup_module_variables_for_maths (${maths_directory} ${json_directory})

  set(CRAYSIM_MATHS_MODULES
    ${SM_HDFDATA_MODULES}
    ${SM_CONFIG_MODULES}
    ${SM_SPLINE_MODULES}
    ${SM_WINDER_MODULES}
    ${SM_GEOMETRY_MODULES}
    ${SM_RANDOM_WALK_MODULES}
    ${SM_JC_VORONOI_MODULES}
  )
  list(REMOVE_DUPLICATES CRAYSIM_MATHS_MODULES)

  set(CRAYSIM_MATHS_DOUBLEHEX_MODULES
    ${SM_CONFIG_MODULES}
    ${SM_BINOMIAL_MODULES}
    ${SM_NM_SIMPLEX_MODULES}
    ${SM_BEZCURVEPATH_MODULES}
    ${SM_HEXGRID_HDF_MODULES}
  )
  list(REMOVE_DUPLICATES CRAYSIM_MATHS_DOUBLEHEX_MODULES)

  set(CRAYSIM_MATHS_ANTBODY_MODULES
    ${SM_CONFIG_MODULES}
  )

  include(${mathplot_directory}/cmake/module_definitions.cmake)
  setup_module_variables_for_mathplot (${mathplot_directory} ${maths_directory} ${json_directory})

  # Should be the mathplot core + anything else.
  set(CRAYSIM_MATHPLOT_MODULES
    ${MPLOT_CORE_MODULES}
    ${mathplot_directory}/mplot/fps/profiler.cppm
    ${craysim_directory}/modules/compoundray/interop.cppm
    ${craysim_directory}/modules/compoundray/Ommatidium.cppm
    ${craysim_directory}/modules/compoundray/ommatidia_data.cppm
    ${craysim_directory}/modules/compoundray/EyeVisual.cppm
    ${mathplot_directory}/mplot/VerticesVisual.cppm
    ${mathplot_directory}/mplot/NormalsVisual.cppm
    ${mathplot_directory}/mplot/InstancedScatterVisual.cppm
  )
  list(REMOVE_DUPLICATES CRAYSIM_MATHPLOT_MODULES)

  set(CRAYSIM_MATHPLOT_DOUBLEHEX_MODULES
    ${mathplot_directory}/mplot/ScatterVisual.cppm
    ${mathplot_directory}/mplot/QuiverVisual.cppm
    ${mathplot_directory}/mplot/HexGridVisual.cppm
    ${mathplot_directory}/mplot/LengthscaleVisual.cppm
  )

  set(CRAYSIM_MODULES
    ${CRAYSIM_MATHS_MODULES}
    ${CRAYSIM_MATHPLOT_MODULES}
    ${craysim_directory}/modules/craysim_visual.cppm
  )
  list(REMOVE_DUPLICATES CRAYSIM_MODULES)

  set(CRAYSIM_DOUBLEHEX_MODULES
    ${CRAYSIM_MATHS_DOUBLEHEX_MODULES}
    ${CRAYSIM_MATHPLOT_DOUBLEHEX_MODULES}
    ${craysim_directory}/modules/craysim_doublehexgrid.cppm
  )
  list(REMOVE_DUPLICATES CRAYSIM_DOUBLEHEX_MODULES)

  set(CRAYSIM_ANTBODY_MODULES
    ${CRAYSIM_MATHS_ANTBODY_MODULES}
    ${craysim_directory}/modules/craysim_antbody.cppm
  )
  list(REMOVE_DUPLICATES CRAYSIM_ANTBODY_MODULES)

endmacro()
