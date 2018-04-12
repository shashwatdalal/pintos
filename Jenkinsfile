pipeline {
  agent { 
    dockerfile true 
  }
  stages {
    stage('Build') {
      steps {
        sh 'make clean'
        sh 'cd src/vm/'
        sh 'echo $PWD'
      }
    }
  }
}
