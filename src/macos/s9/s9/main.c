//
//  main.c
//  s9
//
//  Created by shea on 7/7/24.
//

#include <stdio.h>
#include "s9_app.h"

int main(int argc, const char * argv[]) {	
	s9_app_config_t config = {
		
	};
	
	s9_app_create(&config);
	
	s9_print("hello %d\n", 99);	
	while(s9_app_run() == 0);	
	s9_print("goodbye %d\n", 123);
	
	s9_app_destroy();
	
	return 0;
}
