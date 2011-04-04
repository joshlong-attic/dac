/** 
  Initial work on my attempts to provide an x-platform way to expose the operating system as a client to your application. 
  
  The abstraction should permit: 
   1) letting java code drive a view (determine which files are shown, and perhaps expand the data by adding columns) in explorer/nautilus/finder window:
    1.a.) show files with badges/icons
    1.b.) show folders with badges/icons 
    1.c.) show custom file metadata/properties (this changes per OS - on Gnome this is a property page)
    1.d.) show custom columns
    1.e.) show specialized sidebars (e.g., depending on what's selected...) 
   2) letting java code be triggered when an interesting action happens to the file (this might be less important as Swing Desktop and Adobe AIR already let you do this)
   3) monitor folders for certain types of server side events (this should already be possible with the Spring Integration module)  

http://taschenorakel.de/svn/repos/bulldozer/trunk/documentation/NautilusExtensions.html 
 
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

// utility functions 

void note(char * msg){
  printf(msg, ""); fflush(stdout);
}
 
// interface contract to be a nautilus extension provider 

static GType provider_types[1];

void
nautilus_module_initialize (GTypeModule  *module) 
{
	foo_extension_register_type (module);

	provider_types[0] = foo_extension_get_type ();
}

void 
nautilus_module_shutdown (void)
{
	/* Any module-specific shutdown */
}

void
nautilus_module_list_types (const GType **types,
			    int *num_types)
{
	*types = provider_types;
	*num_types = G_N_ELEMENTS (provider_types);
}


// my implementation specific types
static GType foo_extension_type;


static void
foo_extension_register_type (GTypeModule *module)
{
        static const GTypeInfo info = {
                sizeof (FooExtensionClass),
                (GBaseInitFunc) NULL,
                (GBaseFinalizeFunc) NULL,
                (GClassInitFunc) foo_extension_class_init,
                NULL,
                NULL,
                sizeof (FooExtension),
                0,
                (GInstanceInitFunc) foo_extension_instance_init,
        };
 
	foo_extension_type = g_type_module_register_type (module,
							  G_TYPE_OBJECT,
							  "FooExtension",
							  &info, 0);

        /* ... add interfaces ... */
}

GType
foo_extension_get_type (void)
{
	return foo_extension_type;
}
 
int main () {  
 note( "hello, world! " ) ; 	

} 


