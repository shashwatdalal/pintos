pipeline {
  agent { 
    dockerfile true 
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
