set script_dir [file normalize [file dirname [info script]]]
set project_file [file join $script_dir ad7606_lwip.xpr]
set output_dir [file join $script_dir hardware_platform]
set output_xsa [file join $output_dir ad7606_lwip_ttc.xsa]

open_project $project_file
set_property ip_repo_paths [list [file join $script_dir repo] [file join $script_dir "vitis hls"]] [current_project]
update_ip_catalog

set bd_file [get_files -quiet */design_1.bd]
open_bd_design $bd_file
validate_bd_design
save_bd_design
generate_target all $bd_file

reset_run synth_1
launch_runs synth_1 -jobs 4
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {
    error "Synthesis did not complete"
}

launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "Implementation did not complete"
}

file mkdir $output_dir
open_run impl_1
write_hw_platform -fixed -include_bit -force -file $output_xsa
puts "XSA_READY=$output_xsa"

close_project
exit
