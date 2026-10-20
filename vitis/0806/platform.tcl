# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\Users\weitong\Desktop\finalversion\vitis\0806\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\Users\weitong\Desktop\finalversion\vitis\0806\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {0806}\
-hw {C:\Users\weitong\Desktop\finalversion\vivado\0806.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -out {C:/Users/weitong/Desktop/finalversion/vitis}

platform write
platform generate -domains 
platform active {0806}
bsp reload
bsp setlib -name lwip211 -ver 1.8
bsp write
bsp reload
catch {bsp regenerate}
platform generate
bsp reload
