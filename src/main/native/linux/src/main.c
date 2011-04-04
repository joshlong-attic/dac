/** 
  Initial work on my attempts to provide an x-platform way to expose the operating system as a client to your application. 
  
  The abstraction should permit: 
   1) letting java code drive a view (determine which files are shown, and perhaps expand the data by adding columns) in explorer/nautilus/finder window:
    1.a.) show files with badges/icons
    1.b.) show folders with badges/icons 
    1.c.) show custom file metadata/properties (this changes per OS - on Gnome this is a property page)
    1.d.) show custom columns
   2) letting java code be triggered when an interesting action happens to the file (this might be less important as Swing Desktop and Adobe AIR already let you do this)
   3) monitor folders for certain types of server side events (this should already be possible with the Spring Integration module) 
 
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <malloc.h>
#include <string.h> 
#include <glib-object.h>
#include <glib.h> 


void note(char * msg){
  printf(msg, ""); fflush(stdout);
}
 
 
int main () {  
 note( "hello, world! " ) ; 	

}
