# Recreate an editable source project only. No synthesis, BIT or hardware operations.
set project_dir [file normalize [file dirname [info script]]]
set project_file [file join $project_dir build source_project camera_game.xpr]
if {[file exists $project_file]} {
    open_project $project_file
} else {
    create_project camera_game [file dirname $project_file] -part xc7z020clg400-2
    set_property ip_repo_paths [list [file join $project_dir repo ip]] [current_project]
    update_ip_catalog
    add_files -norecurse [file join $project_dir rtl axis_color_highlight.v]
    add_files -norecurse [file join $project_dir color_test.srcs sources_1 bd system system.bd]
    add_files -norecurse [file join $project_dir color_test.srcs sources_1 bd system hdl system_wrapper.v]
    foreach name {system.xdc mipi.xdc hdmi_out.xdc display720.xdc debug_hub.xdc} {
        add_files -fileset constrs_1 -norecurse [file join $project_dir color_test.srcs constrs_1 new $name]
    }
    set_property IS_ENABLED false [get_files -quiet */debug_hub.xdc]
    set_property top system_wrapper [current_fileset]
    update_compile_order -fileset sources_1
}
puts "SOURCE_PROJECT_ONLY NO_SYNTHESIS_NO_HARDWARE"
