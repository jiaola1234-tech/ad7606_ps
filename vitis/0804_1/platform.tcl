# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\Users\DELL\Desktop\finalversion\vitis\0804_1\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\Users\DELL\Desktop\finalversion\vitis\0804_1\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {0804_1}\
-hw {C:\Users\DELL\Desktop\finalversion\hardwareplatform\0804final.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -out {C:/Users/DELL/Desktop/finalversion/vitis}

platform write
platform generate -domains 
platform active {0804_1}
domain active {zynq_fsbl}
bsp reload
domain active {standalone_domain}
bsp reload
bsp setlib -name lwip211 -ver 1.8
bsp write
bsp reload
catch {bsp regenerate}
platform generate
catch {platform remove 0804}
platform generate
platform generate
platform generate
platform generate
