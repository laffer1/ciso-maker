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

        stage('Test') {
            steps {
                sh 'kyua test -k Kyuafile'
                sh 'kyua report'
            }
        }
    }

    post {
        success {
            archiveArtifacts artifacts: 'ciso-maker'
        }
    }
}
