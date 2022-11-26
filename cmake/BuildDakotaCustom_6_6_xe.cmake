##############################################################################
#
# Template CMake Configuration File.
#
##############################################################################
# The following CMake variables represent the minimum set of variables
# that are required to allow Dakota to
#   * find all prerequisite third party libraries (TPLs)
#   * configure compiler and MPI options
#   * set Dakota install path
#
# Instructions:
# 1. Read Dakota/INSTALL - Source Quick Start to use this template file.
#
# 2. Uncomment CMake variables below ONLY for values you need to change for
#    your platform. Edit variables as needed.
#
#    For example, if you are using a custom install of Boost, installed in
#    /home/me/usr/boost, uncomment both CMake Boost variables  and edit
#    paths:
#       set(BOOST_ROOT
#           "/home/me/usr/boost"
#           CACHE PATH "Use non-standard Boost install" FORCE)
#       set( Boost_NO_SYSTEM_PATHS TRUE
#            CACHE BOOL "Supress search paths other than BOOST_ROOT" FORCE)
#
#    Save file and exit.
#
# 6. Run CMake with script file. At terminal window, type:
#      $ cmake -C BuildCustom.cmake $DAK_SRC
#
#    If you have not followed instructions in INSTALL -Source Quick Start,
#    you will need to replace BuildCustom.cmake with the actual filename of
#    this file and $DAK_SRC with the actual path to Dakota source.
#
##############################################################################

##############################################################################
# Set BLAS, LAPACK library paths ONLY if in non-standard locations. For MKL,
# set both BLAS_LIBS and LAPACK_LIBS to the appropriate link line. (If any
# C++ compiler options are needed by MKL, use CMAKE_CXX_FLAGS.)
##############################################################################
#set( BLAS_LIBS
#      "/usr/lib64/libblas.so"
#      CACHE FILEPATH "Use non-standard BLAS library path" FORCE )

#set( LAPACK_LIBS
#      "/usr/lib64/liblapack.so"
#      CACHE FILEPATH "Use non-standard BLAS library path" FORCE )

##############################################################################
# Set additional compiler options
# Uncomment and replace <flag> with actual compiler flag, e.g. -xxe4.2
##############################################################################

set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2"
    CACHE STRING "C Flags my platform" )

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2"
    CACHE STRING "CXX Flags for my platform" )

set( CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -O2"
    CACHE STRING "Fortran Flags for my platform" )

# To build shared libraries ONLY
# BUILD_STATIC_LIBS:BOOL=OFF
# BUILD_SHARED_LIBS:BOOL=ON

# Build static libraries ONLY
# BUILD_STATIC_LIBS:BOOL=ON
# BUILD_SHARED_LIBS:BOOL=OFF

set( HAVE_ACRO   OFF CACHE BOOL "HAVE_ACRO" FORCE)
set( HAVE_AMPL   OFF CACHE BOOL "HAVE_AMPL" FORCE)
set( HAVE_CONMIN OFF CACHE BOOL "HAVE_CONMIN" FORCE)
set( HAVE_DDACE  OFF CACHE BOOL "HAVE_DDACE" FORCE)



set( HAVE_DOT    OFF CACHE BOOL "HAVE_DOT" FORCE)
set( HAVE_NCSU   OFF CACHE BOOL "HAVE_NCSU" FORCE)
set( HAVE_NL2SOL OFF CACHE BOOL "HAVE_NL2SOL" FORCE)


# HAVE_ACRO       ON
# HAVE_AMPL       ON  May need to be OFF if compiling with C99 support
# HAVE_CONMIN     ON
# HAVE_DDACE      ON
# HAVE_DFFTPACK   ON
# HAVE_DOT        ON  Turns OFF if packages/DOT missing
# HAVE_DREAM      ON
# HAVE_FFTW       OFF  Off due to dependence on GPL-licensed FFTW
# HAVE_FSUDACE    ON
# HAVE_HOPSPACK   ON
# HAVE_JEGA       ON
# HAVE_LHS        ON
# HAVE_NCSU       ON
# HAVE_NL2SOL     ON
# HAVE_NLPQL      ON   Turns OFF if packages/NLPQL missing
# HAVE_NOMAD      ON
# HAVE_NPSOL      ON   Turns OFF if packages/NPSOL missing
# HAVE_OPTPP      ON
# HAVE_PECOS      ON   Currently required
# HAVE_PSUADE     ON
# HAVE_QUESO      OFF  Off due to dependence on GPL-licensed GSL
# DAKOTA_HAVE_GSL OFF  Required when HAVE_QUESO=ON
# HAVE_SURFPACK   ON   Currently required

##############################################################################
# Set MPI options
# Recommended practice is to set DAKOTA_HAVE_MPI and set MPI_CXX_COMPILER
# to a compiler wrapper.
##############################################################################
#set( DAKOTA_HAVE_MPI ON
#     CACHE BOOL "Build with MPI enabled" FORCE)

#set( MPI_CXX_COMPILER "path/to/mpicxx"
#     CACHE FILEPATH "Use MPI compiler wrapper" FORCE)


##############################################################################
# Set Boost path if CMake cannot find your installed version of Boost or
# if you have a custom Boost install location.
##############################################################################

set(BOOST_ROOT
   "/home/bellout/git/EQN/downloads/boost_1_58_0_t0"
   CACHE PATH "Use non-standard Boost install" FORCE)

set(Boost_NO_SYSTEM_PATHS TRUE
    CACHE BOOL "Supress search paths other than BOOST_ROOT" FORCE)

##############################################################################
# Set Trilinos path if you want have a custom Trilinos install location. If
# not set, the Trilinos package, teuchos, will be build during the Dakota
# build.
##############################################################################
#set( Trilinos_DIR
#      "path/to/Trilinos/install"
#      CACHE PATH "Path to installed Trilinos" FORCE )

##############################################################################
# Customize DAKOTA
##############################################################################
set( CMAKE_INSTALL_PREFIX
     "/home/bellout/git/EQN/packages"
     CACHE PATH "Path to Dakota installation" )
