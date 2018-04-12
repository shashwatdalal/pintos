pipeline {
  agent { 
    dockerfile {
      filename 'Dockerfile'
      additionalBuildArgs '-v /pintos:$PWD'
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
