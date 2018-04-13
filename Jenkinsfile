pipeline {
  agent {
    dockerfile true
  }
  stages {
   stage('Docker Tests') {
     steps {
       sh 'gdb'
     }
   }
   stage('Build') {
     steps{
       sh 'cd src/vm/ && make'
     }
   }
   stage('Test') {
    steps {
      sh 'tree'
    }
   }
  }
}
