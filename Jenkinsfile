pipeline {
    agent any

    options {
        timeout(time: 10, unit: 'MINUTES')
    }

    stages {
        stage('Build') {
            steps {
                sh "make clean && make CFLAGS='-Wall -Wextra -Wconversion -Wshadow -Wstrict-prototypes -pedantic -std=c99 -O2'"
            }
        }

        stage('Smoke Test') {
            steps {
                sh 'truncate -s 2048 tiny.iso'
                sh './ciso-maker tiny.iso tiny.cso'
                sh './ciso-maker -x tiny.cso tiny.out.iso'
                sh 'cmp -s tiny.iso tiny.out.iso'
            }
        }
    }

    post {
        success {
            archiveArtifacts artifacts: 'ciso-maker'
        }
    }
}
