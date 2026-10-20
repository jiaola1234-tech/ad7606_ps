set script_dir [file normalize [file dirname [info script]]]
set project_file [file join $script_dir ad7606_lwip.xpr]

open_project $project_file
set_property ip_repo_paths [list [file join $script_dir repo] [file join $script_dir "vitis hls"]] [current_project]
update_ip_catalog

set bd_file [get_files -quiet */design_1.bd]
if {[llength $bd_file] != 1} {
    error "design_1.bd was not found uniquely"
}

open_bd_design $bd_file
validate_bd_design

set ps7 [get_bd_cells -quiet processing_system7_0]
if {[llength $ps7] != 1} {
    error "processing_system7_0 was not found"
}

set ttc_enabled [get_property CONFIG.PCW_TTC0_PERIPHERAL_ENABLE $ps7]
set ttc_io [get_property CONFIG.PCW_TTC0_TTC0_IO $ps7]
puts "CHECK_OK: block design is valid"
puts "TTC0_ENABLE=$ttc_enabled"
puts "TTC0_IO=$ttc_io"

close_project
exit

