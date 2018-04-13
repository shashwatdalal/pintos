pipeline {
  agent {
    dockerfile true
  }
  stages {
   stages('Docker Tests') {
     steps {
       sh 'gdb'
     }
   }
   stage('Build') {
     steps{
       sh 'cd src/vm/ && make'
     }
   }
  }
}
