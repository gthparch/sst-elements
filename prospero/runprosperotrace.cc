#include <sst_config.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <vector>
#include <string>

void printUsage() {
	printf("sst-prospero-trace [trace options] -- [app] [app options]\n");
	printf("\n");
	printf("Trace-Options:\n");
	printf("  -o <file>     Name of trace output files.\n");
	printf("  -f <format>   Output <format> = {text, binary, compressed}\n");
	printf("  -t <maxthr>   Maximum number of threads to trace, if not set will search for OMP_NUM_THREADS or set to 1\n");
	printf("\n");
}

int main(int argc, char* argv[]) {

	for(int i = 1; i < argc; i++) {
		if(std::strcmp(argv[i], "--") == 0) {
			break;
		} else if(std::strcmp(argv[i], "--help") == 0 ||
			std::strcmp(argv[i], "-help") == 0 ||
			std::strcmp(argv[i], "-h") == 0) {

			printUsage();
			exit(0);
		}
	}

	char* pinToolMarker = const_cast<char*>("-t");
	char* doubleDash = const_cast<char*>("--");
	char* numberOne = const_cast<char*>("1");

	bool foundMarker = false;
	std::vector<char*> prosParams;
	std::vector<char*> appParams;
	char* appPath;

	char* toolPath = (char*) malloc(sizeof(char) * PATH_MAX);

#ifdef SST_COMPILE_MACOSX
	sprintf(toolPath, "%s/libexec/prosperotrace.dylib", SST_INSTALL_PREFIX);
#else
	sprintf(toolPath, "%s/libexec/prosperotrace.so", SST_INSTALL_PREFIX);
#endif

	appParams.push_back(const_cast<char*>(PINTOOL_EXECUTABLE));
	appParams.push_back(pinToolMarker);
	appParams.push_back(toolPath);

	for(int i = 1; i < argc; i++) {
		if(foundMarker) {
			appParams.push_back(argv[i]);
		} else {
			if(std::strcmp(argv[i], doubleDash) == 0) {
				foundMarker = true;

				if(i == ( argc - 1 )) {
					fprintf(stderr, "Found -- marker, but no application path in options\n");
					exit(-1);
				} else {
					appParams.push_back(argv[i]);
					appParams.push_back(argv[i+1]);
					appPath = argv[i+1];

					i++;
				}
			} else {
				prosParams.push_back(argv[i]);
			}
		}
	}

	if(! foundMarker) {
		fprintf(stderr, "Error: did not find the -- marker which separates out the application and profiling options\n");
		printUsage();
		exit(-1);
	}

	int threadCount = 0;
	char* outputFile = const_cast<char*>("prospero-trace");
	char* outputFormat = const_cast<char*>("text");

	for(int i = 0; i < prosParams.size(); i++) {
		if( std::strcmp(prosParams[i], "-t") == 0 ) {
			if(i == (prosParams.size() - 1) ) {
				fprintf(stderr, "-t needs a number of threads to be specified\n");
				exit(-1);
			} else {
				threadCount = std::atoi(prosParams[i+1]);
				i++;
			}
		} else if( std::strcmp(prosParams[i], "-o") == 0 ) {
			if(i == (prosParams.size() - 1) ) {
				fprintf(stderr, "-o needs a file to be specified\n");
				exit(-1);
			} else {
				outputFile = prosParams[i+1];
				i++;
			}
		} else if( std::strcmp(prosParams[i], "-f") == 0) {
			if(i == (prosParams.size() - 1) ) {
				fprintf(stderr, "-f needs a format to be specified\n");
				exit(-1);
			} else {
				if(std::strcmp(prosParams[i+1], "text") == 0 ||
					std::strcmp(prosParams[i+1], "binary") == 0 ||
					std::strcmp(prosParams[i+1], "compressed") == 0) {

					outputFormat = prosParams[i+1];
					i++;
				} else {
					fprintf(stderr, "Error: output format %s is not valid\n",
						prosParams[i+1]);
					printUsage();
					exit(-1);
				}
			}
		} else {
			fprintf(stderr, "Error: program option: %s\n",
				prosParams[i]);
			printUsage();
			exit(-1);
		}
	}

	if(0 == threadCount) {
		// User did not set this
		if(NULL == getenv("OMP_NUM_THREADS")) {
			prosParams.push_back(pinToolMarker);
			prosParams.push_back(numberOne);
		} else {
			prosParams.push_back(pinToolMarker);
			char* ompThreadCount = (char*) malloc(sizeof(char) * 8);
			sprintf(ompThreadCount, "%d", getenv("OMP_NUM_THREADS"));
			prosParams.push_back(ompThreadCount);
		}
	}

	printf("===============================================================\n");
	printf("Prospero Memory Tracing Tool\n");
	printf("Part of the Structural Simulation Toolkit (SST)\n");
	printf("===============================================================\n");
	printf("\n");

	pid_t childProcess;
	childProcess = fork();

	if(childProcess < 0) {
		perror("fork");
		fprintf(stderr, "Error: Failed to launch the child process.\n");
		exit(-1);
	}

	if(childProcess != 0) {
		// The process has been launched and we need to wait for it
		int processStat = 0;
		pid_t childRC = waitpid(childProcess, &processStat, 0);

		printf("\n");
		printf("===============================================================\n");

		if( childRC > 0 ) {
			printf("Child process(es) for profiling completed.\n");
			printf("===============================================================\n");
		} else {
			fprintf(stderr, "Error: Unable to start PIN.\n");
			perror("waitpid");
			exit(-1);
		}

	} else {
		printf("IN THE ZERO CASE!\n");

		appParams.push_back(NULL);

		char** paramsArray = (char**) malloc(sizeof(char*) * (appParams.size() + prosParams.size()));
		int nextParamsArray = 0;

		for(auto i = 0; i < appParams.size(); ++i) {
			printf("Iteration: %d [%s]\n", i, appParams[i]);

			// Before we copy in the application parameters, we need to copy in the
			// the options for the tool itself
			if( (NULL != appParams[i]) && (std::strcmp(appParams[i], "--") == 0) ) {
				for(auto j = 0; j < prosParams.size(); j++) {
					paramsArray[nextParamsArray] = prosParams[j];
					nextParamsArray++;
				}
			}

			paramsArray[nextParamsArray] = appParams[i];
			nextParamsArray++;
		}

		printf("Prospero will run the tracing command:\n");

		for(auto i = 0; i < (appParams.size() + prosParams.size()); ++i) {
			if(NULL != paramsArray[i]) {
				printf("%s ", paramsArray[i]);
			} else {
				printf("(NULL) ");
			}
		}

		printf("\n");

		int executeRC = execvp(PINTOOL_EXECUTABLE, paramsArray);
		printf("Executing application returns %d.\n", executeRC);

		free(paramsArray);
	}

	return 0;
}

