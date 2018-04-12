pipeline {
  agent { 
    dockerfile {
      args: '-v /pintos:$PWD'
    }
  }
  stages {
    stage('Example') {
      steps {
        echo 'Hello World'
      }
    }
  }
}
