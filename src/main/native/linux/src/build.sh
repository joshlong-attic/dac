
## 
## builds the code for this simple integration 
## 

touch  fsbrowser-client; rm fsbrowser-client;

gcc main.c -ggdb -m64 `pkg-config --cflags libnautilus-extension` -o fsbrowser-client 



