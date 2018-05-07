pipeline {
  agent none
  stages {
    stage('Build Dockerfile') {
      agent any
      steps { sh 'docker build -t pintos .' }
    }
    stage('Build Source') {
      agent { docker { image 'pintos:latest' } }
      steps { sh 'cd src/vm/ && make' }
    }
    stage('Test') {
      agent { docker { image 'pintos:latest' } }
      steps { sh 'tree'}
    }
  }
}
