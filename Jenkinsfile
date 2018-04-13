pipeline {
  agent {
    // Equivalent to "docker build -f Dockerfile.build --build-arg version=1.0.2 ./build/
    dockerfile {
        filename 'pintos'
        additionalBuildArgs  '-t'
    }
  }
  stages {
   stage('Build') {
     steps{
       sh 'pwd'
     }
   }
  }
}
