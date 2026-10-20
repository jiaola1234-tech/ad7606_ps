# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\Users\weitong\Desktop\finalversion\vitis\0806_1\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\Users\weitong\Desktop\finalversion\vitis\0806_1\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {0806_1}\
-hw {C:\Users\weitong\Desktop\finalversion\0806.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -out {C:/Users/weitong/Desktop/finalversion/vitis}

platform write
platform generate -domains 
platform active {0806_1}
bsp reload
bsp setlib -name lwip211 -ver 1.8
bsp removelib -name lwip211
bsp setlib -name lwip211 -ver 1.8
bsp write
bsp reload
catch {bsp regenerate}
platform generate
