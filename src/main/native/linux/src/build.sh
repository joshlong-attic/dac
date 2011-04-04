
## 
## builds the code for this simple integration 
## 
## to add configuration for others of interest, simply search; /usr/lib64/pkgconfig/* 
touch  fsbrowser-client; rm fsbrowser-client;

export O="fsclient"
PCS="libgnomeui-2.0  glib-2.0 gtk+-2.0 libglade-2.0 libnautilus-extension gnome-vfs-2.0 gnome-vfs-module-2.0 gnome-system-tools"
PCS_CMDS=""
for P in $PCS;
 do
 export PCS_CMDS="$PCS_CMDS ``pkg-config --cflags $P`` " ;
done 
echo "$PCS_CMDS" 

gcc `$PCS_CMDS` -ggdb -m64 main.c -o $O


#export CF="`pkg-config --cflags libglade-2.0` `pkg-config --cflags libnautilus-extension` `pkg-config --cflags gnome-vfs-2.0`  `pkg-config --cflags gnome-vfs-module-2.0.pc` `pkg-config gnome-system-tools` "  
#echo $CF
#gcc  -ggdb -m64 $CF main.c -o fsbrowser-client 

 

