pipeline {
  agent { 
    dockerfile true 
  }
  stages {
    def customImage = docker.build("my-image:${env.BUILD_ID}")
    stage('Build') {
      customImage.inside {
        sh 'cd src/vm/'
        sh 'echo $PWD'
      }
    }
  }
}
