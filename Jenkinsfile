pipeline {
  agent { 
    dockerfile {
      filename 'Dockerfile'
      additionalBuildArgs '-v $PWD:/pintos'
    }
  }
  stages {
    stage('Build') {
      steps {
        bash '''#!/bin/bash
                 cd src/vm/
                 make
         '''
      }
    }
  }
}
