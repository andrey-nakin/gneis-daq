cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(RScripts VERSION 1.0.0 LANGUAGES CXX DESCRIPTION "R scripts")

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.R)

if(DEFINED R_SCRIPTS_INSTALL_DIR)
	install(
		FILES ${SOURCES} 
		DESTINATION ${R_SCRIPTS_INSTALL_DIR}
	)
endif(DEFINED R_SCRIPTS_INSTALL_DIR)
