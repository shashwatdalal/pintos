pipeline {
  agent {
    dockerfile true
  }
  stages {
   stage('Build') {
     steps{
       sh 'cd src/vm/ && make'
     }
   }
  }
}
