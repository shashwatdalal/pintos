pipeline {
  agent { 
    dockerfile true 
  }
  stages {
   stage('Build') {
     def customImage = docker.build("my-image:${env.BUILD_ID}")
     customImage.inside {
        sh 'cd src/vm/'
        sh 'echo $PWD'
      }
    }
  }
}
