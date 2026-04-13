#
# Define variables of module groups for use by client projects.
#
macro(setup_module_variables_for_craysim_maths base_directory)
  set(CRAYSIM_MATHS_MODULES
    ${base_directory}/sm/config.cppm
    ${base_directory}/sm/hdfdata.cppm
  )
endmacro()

macro(setup_module_variables_for_craysim_mathplot base_directory)
  set(CRAYSIM_MATHPLOT_MODULES
    ${base_directory}/mplot/fps/profiler.cppm
  )
endmacro()

macro(setup_module_variables_for_craysim base_directory)
  set(CRAYSIM_MODULES
    ${base_directory}/modules/tk_spline.cppm           # GPL v2
    ${base_directory}/modules/craysim_random_walk.cppm # Uses tk_spline, GPL v2
    ${base_directory}/modules/craysim_visual.cppm      # Uses random_walk, GPL v2
    ${base_directory}/modules/craysim_antbody.cppm
  )
endmacro()
