/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.cpp
 * Author: ahan
 *
 * Created on September 25, 2017, 8:23 PM
 */

#include <cstdio>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <pthread.h>
#include <semaphore.h>
#include <vector>
#include <ctime>
#include <sstream>

bool isSoldOut(int thread_number);
void rowColumnInitializer(int* row, int* column, int seller_id);
void iterateNextSeat(int* row, int* column, int seller_id);
int calculateWaitTime(int seller_id);
void printSeats(std::string seller_name, int buyer_time);
void* seller_function(void *arg);
void* customer_function(void *arg);

const int RUNTIME = 60; // number of simulated minutes
const int LOW = 6; //number of low price sellers
const int MED = 3; //number of medium price sellers
const int HIGH = 1; //number of high price sellers
const int SELLERS = LOW + MED + HIGH; //total number of sellers

struct seat {
	std::string seat_seller; // name of seller who sold seat
	bool isSold; // true if seat is already sold
};

pthread_mutex_t seat_mutex; // mutex for protecting access to seats

struct seat seats[10][10]; //100 total seats

pthread_mutex_t print_mutex; //mutex that protects access to printf; declutters output screen

int current_time = 0; // current time, measured in minutes

struct thread_args {
	int seed; // random number generator seed
	int buyer_number; // the total number of buyers per seller
	int seller_id; // id of the seller
};

struct thread_args my_args[SELLERS]; //arguments to low/med/high functions

struct customer_arg {
	int seed; //random number generator seed
	int seller_id; // id of the seller that corresponds to customer
	int customer_id; // id of customer
};

sem_t enter_time[SELLERS]; //semaphores that control entry into main timer thread

struct more_sem {
	sem_t enter_seller; //semaphore for controlling entry into seller function
	sem_t enter_customer; //semaphore for controlling entry into customer function
};

struct more_sem* new_semaphores[SELLERS];

std::vector<std::vector<int> > buyer_queue; // list of buyer (customer) lists for each seller
std::vector<std::vector<pthread_t> > customer_threads; // list of customer thread lists for each seller

pthread_mutex_t buyer_mutex[SELLERS]; // mutex that protects customer line for each seller

std::string all_names[SELLERS] = { "L1", "L2", "L3", "L4", "L5", "L6", "M1",
		"M2", "M3", "H0" };

int main(int argc, char** argv) {
	int buyer_number;
	if (argc > 1) {
		buyer_number = atoi(argv[1]);
	} else {
		buyer_number = 15;
	}
	if (buyer_number <= 0) {
		std::cout << "Buyer number must be greater than or equal to zero.\n";
		exit(EXIT_FAILURE);
	}

	// initialize semaphores for entering main time thread
	for (int i = 0; i < SELLERS; i++) {
		int res1 = sem_init(&(enter_time[i]), 0, 0);
		if (res1 != 0) {
			perror("Semaphore initialization failed");
			printf("Semaphore initialization failed");
			exit(EXIT_FAILURE);
		}
	}

	// initialize seat mutex
	int res = pthread_mutex_init(&seat_mutex, NULL);
	if (res != 0) {
		perror("Mutex initialization failed");
		printf("Mutex initialization failed");
		exit(EXIT_FAILURE);

	}

	// initialize each seat
	for (int i = 0; i < 10; i++) {
		for (int j = 0; j < 10; j++) {
			seats[i][j].isSold = false;
			seats[i][j].seat_seller = "";
		}
	}

	// initialize print mutex
	res = pthread_mutex_init(&print_mutex, NULL);
	if (res != 0) {
		perror("Mutex initialization failed");
		printf("Mutex initialization failed");
		exit(EXIT_FAILURE);
	}

	// initialize mutexes that protect lines for each buyer
	for (int i = 0; i < SELLERS; i++) {
		int res = pthread_mutex_init(&buyer_mutex[i], NULL);
		if (res != 0) {
			perror("Mutex initialization failed");
			printf("Mutex initialization failed");
			exit(EXIT_FAILURE);
		}
	}

	// customer thread arguments must be heap allocated so customer thread can access them
	struct customer_arg* new_customer_args[SELLERS];
	for (int i = 0; i < SELLERS; i++) {
		new_customer_args[i] = new customer_arg[buyer_number];
	}

	//heap allocate semaphores
	for (int i = 0; i < SELLERS; i++) {
		new_semaphores[i] = new more_sem[buyer_number];
	}

	//place all seller lists inside another list
	for (int i = 0; i < SELLERS; i++) {
		std::vector<int> a_queue;
		buyer_queue.push_back(a_queue);
	}

	//place threads in list, initialize semaphores, and set customer thread function arguments
	for (int i = 0; i < SELLERS; i++) {
		std::vector<pthread_t> some_threads;
		customer_threads.push_back(some_threads);
		for (int j = 0; j < buyer_number; j++) {

			pthread_t customer_thread;
			customer_threads.back().push_back(customer_thread);

			int res = sem_init(&(new_semaphores[i] + j)->enter_customer, 0, 1);
			if (res != 0) {
				perror("semaphore intialization failed");
				printf("semaphore initialization failed");
				exit(EXIT_FAILURE);
			}

			res = sem_init(&(new_semaphores[i] + j)->enter_seller, 0, 0);
			if (res != 0) {
				perror("semaphore initialization failed");
				printf("sempahore initialization failed");
				exit(EXIT_FAILURE);
			}

			struct customer_arg an_arg;
			an_arg.seed = i * j;
			an_arg.seller_id = i;
			an_arg.customer_id = j;
			//*(new_customer_args[i]+j) = an_arg;
			new_customer_args[i][j] = an_arg;
			res = pthread_create(&customer_threads[i][j], NULL,
			//customer_function, (void *) new_customer_args[i]+j);
					customer_function, (void *) &new_customer_args[i][j]);
			if (res != 0) {
				perror("thread creation failed");
				printf("thread creation failed");
				exit(EXIT_FAILURE);
			}
		}
	}

	//initialize arguments for low price seller functions and create low prices seller function threads
	pthread_t my_threads[SELLERS];
	for (int i = 0; i < LOW; i++) {
		my_args[i].seed = i;
		my_args[i].buyer_number = buyer_number;
		my_args[i].seller_id = i;
		int res = pthread_create(&my_threads[i], NULL, seller_function,
				(void *) &my_args[i]);
		if (res != 0) {
			perror("Thread creation failed");
			printf("Thread creation failed");
			exit(EXIT_FAILURE);
		}
	}

	//initialize arguments for medium price seller functions and create medium prices seller function threads
	for (int i = 0; i < MED; i++) {
		int j = 6 + i;
		my_args[j].seed = j;
		my_args[j].buyer_number = buyer_number;
		my_args[j].seller_id = j;
		int res = pthread_create(&my_threads[j], NULL, seller_function,
				(void *) &my_args[j]);
		if (res != 0) {
			perror("Thread creation failed");
			printf("Thread creation failed");
			exit(EXIT_FAILURE);
		}
	}

	//initialize arguments for high price seller function and create medium prices seller function thread
	int k = 9;
	my_args[k].seed = k;
	my_args[k].buyer_number = buyer_number;
	my_args[k].seller_id = k;
	res = pthread_create(&my_threads[k], NULL, seller_function,
			(void *) &my_args[k]);
	if (res != 0) {
		perror("Thread creation failed");
		printf("Thread creation failed");
		exit(EXIT_FAILURE);
	}

	// increment time for timer thread in loop; wait on signal from sellers and send signal to customers using semaphore
	do {
		for (int i = 0; i < SELLERS; i++) {
			int res = sem_wait(&(enter_time[i]));
			if (res != 0) {
				perror("Semaphore failed to wait");
				printf("Semaphore failed to wait");
				exit(EXIT_FAILURE);
			}
		}
		current_time++;
		for (int i = 0; i < SELLERS; i++) {
			for (int j = 0; j < buyer_number; j++) {
				int res = sem_post(&(new_semaphores[i] + j)->enter_customer);
			}
			if (res != 0) {
				perror("Semaphore failed to post");
				printf("Semaphore failed to post");
				exit(EXIT_FAILURE);
			}
		}
	} while (current_time < RUNTIME);

	//wait for all seller threads to finish before closing
	for (int i = 0; i < SELLERS; i++) {
		pthread_join(my_threads[i], NULL);
	}

	exit(EXIT_SUCCESS);
}

//determines if tickets are sold out
bool isSoldOut(int seller_id) {
	bool isSoldOut = true;
	for (int i = 0; i < 10; i++) {
		for (int j = 0; j < 10; j++) {
			if (seats[i][j].seat_seller.compare("") == 0) {
				isSoldOut = false;
				break;
			}

		}
		if (isSoldOut == false)
			break;
	}
#ifdef DEBUG
	if(isSoldOut) {
		printf("Thread %d has sold out.\n", seller_id);
	}
#endif
	return isSoldOut;
}

//initialize the values for row and column
void rowColumnInitializer(int* row, int* column, int seller_id) {
	switch (seller_id) {
	case 9:
		*column = 0;
		*row = 0;
		break;
	case 8:
	case 7:
	case 6:
		*column = 0;
		*row = 4;
		break;
	default:
		*column = 0;
		*row = 9;

	}

}

//iterate through the seat for the row and column of the next seat
void iterateNextSeat(int* row, int* column, int seller_id) {
	switch (seller_id) {
	case 9:
		if (*column == 9) {
			(*row)++;
			*column = 0;
		} else
			(*column)++;
		break;
	case 8:
	case 7:
	case 6:
		if (*column == 9) {
			switch (*row) {
			case 4:
				*row = 5;
				break;
			case 5:
				*row = 3;
				break;
			case 3:
				*row = 6;
				break;
			case 6:
				*row = 2;
				break;
			case 2:
				*row = 7;
				break;
			case 7:
				*row = 1;
				break;
			case 1:
				*row = 8;
				break;
			case 8:
				*row = 0;
				break;
			case 0:
				*row = 9;
				break;
			default:
				(*row)++;
				break;
			}
			*column = 0;
		} else
			(*column)++;
		break;
	default:
		if (*column == 9) {
			(*row)--;
			*column = 0;
		} else
			(*column)++;
	}

}

// calculate how long seller takes to finish completing transaction with customer
int calculateWaitTime(int seller_id) {
	int waitUntil = 0;

	switch (seller_id) {
	case 9:
		waitUntil = current_time + rand() % 2 + 1;
		break;
	case 8:
	case 7:
	case 6:
		waitUntil = current_time + rand() % 3 + 2;
		break;
	default:
		waitUntil = current_time + rand() % 4 + 4;
	}

	return waitUntil;
}

//print which seller sold which seat in layout of seat diagram
void printSeats(std::string seller_name, int buyer_time) {
	int res = pthread_mutex_lock(&print_mutex);
	if (res != 0) {
		perror("print mutex failed to lock");
		printf("print mutex failed to lock\n");
		exit(EXIT_FAILURE);
	}
	std::cout << "0:" << std::setfill('0') << std::setw(2) << current_time;
	std::cout << "; " << seller_name << " sold ticket ";
	std::cout << "to customer who arrived at 0:";
	std::cout << std::setfill('0') << std::setw(2) << buyer_time;
	std::cout << std::endl;

	for (int i = 0; i < 10; i++) {
		for (int j = 0; j < 10; j++) {
			if (seats[i][j].seat_seller.compare("") != 0)
				std::cout << " " << seats[i][j].seat_seller;
			else
				std::cout << " ----";
		}
		std::cout << std::endl;
	}
	res = pthread_mutex_unlock(&print_mutex);
	if (res != 0) {
		perror("print mutex failed to unlock");
		printf("print mutex failed to unlock\n");
		exit(EXIT_FAILURE);
	}
}

//seller function
void* seller_function(void *arg) {
	struct thread_args *args = (thread_args*) arg;

	int waitUntil = 0;
	bool finishedHelpingCustomer = true;
	int row;
	int column;
	rowColumnInitializer(&row, &column, args->seller_id);
	int ticketsSold = 0;

	//run loop for 60 simulated minutes
	while (current_time < RUNTIME) {
		//wait for signal from customer threads
		for (int i = 0; i < args->buyer_number; i++) {
			int res = sem_wait(
					&(new_semaphores[args->seller_id] + i)->enter_seller);
			if (res != 0) {
				perror("Semaphore failed to wait");
				printf("Semaphore failed to wait\n");
				exit(EXIT_FAILURE);
			}
		}

		// if seller is available to sell tickets and there are customers to sell to, then sell tickets
		if (waitUntil <= current_time
				&& !buyer_queue[args->seller_id].empty()) {
			//if not done helping customer, finish helping customer who was already assigned seat;
			//print seat layout and send customer away
			if (finishedHelpingCustomer == false) {
				finishedHelpingCustomer = true;
				std::ostringstream ticketStream;
				ticketStream << std::setfill('0') << std::setw(2)
						<< ticketsSold;
				std::string tickets = ticketStream.str();
				seats[row][column].seat_seller = all_names[args->seller_id]
						+ tickets;
				ticketsSold++;
				pthread_mutex_lock(&buyer_mutex[args->seller_id]);
				printSeats(all_names[args->seller_id],
						buyer_queue[args->seller_id].front());
				buyer_queue[args->seller_id].erase(
						buyer_queue[args->seller_id].begin());
				pthread_mutex_unlock(&buyer_mutex[args->seller_id]);

				iterateNextSeat(&row, &column, args->seller_id);
			}

			//lock ticket and check if ticket is available to be sold; if it is, assign ticket to customer
			if (!buyer_queue[args->seller_id].empty()) {
				bool finishedLockingTicket = false;
				int res = pthread_mutex_lock(&seat_mutex);
				if (res != 0) {
					perror("Failed to lock seat mutex");
					printf("Failed to lock seat mutex");
					exit(EXIT_FAILURE);
				}
				while (!finishedLockingTicket) {
					//finished iterating through all seats; send all customers away
					if (row > 9 || row < 0) {
						if (!buyer_queue[args->seller_id].empty()) {
							res = pthread_mutex_lock(&print_mutex);
							if (res != 0) {
								perror("Failed to lock print mutex");
								printf("Failed to lock print mutex");
								exit(EXIT_FAILURE);
							}
							printf(
									"0:%02d; Out of seats. Customer at %s leaves.\n",
									current_time,
									all_names[args->seller_id].c_str());
							res = pthread_mutex_unlock(&print_mutex);
							if (res != 0) {
								perror("Failed to unlock print mutex");
								printf("Failed to unlock print mutex");
								exit(EXIT_FAILURE);
							}
							res = pthread_mutex_lock(
									&buyer_mutex[args->seller_id]);
							if (res != 0) {
								perror("Failed to lock buyer mutex");
								printf("Failed to lock buyer mutex");
								exit(EXIT_FAILURE);
							}
							buyer_queue[args->seller_id].erase(
									buyer_queue[args->seller_id].begin());
							res = pthread_mutex_unlock(
									&buyer_mutex[args->seller_id]);
							if (res != 0) {
								perror("Failed to unlock buyer mutex");
								printf("Failed to unlock buyer mutex");
								exit(EXIT_FAILURE);
							}
						}
						break;
					}

					//if seat not sold, assign ticket to customer, otherwise, look for next ticket
					if (seats[row][column].isSold == false) {
						seats[row][column].isSold = true;
						finishedLockingTicket = true;
						waitUntil = calculateWaitTime(args->seller_id);
						finishedHelpingCustomer = false;
						int res = pthread_mutex_lock(&print_mutex);
						if (res != 0) {
							perror("Failed to lock print mutex");
							printf("Failed to lock print mutex");
							exit(EXIT_FAILURE);
						}
						printf(
								"0:%02d; Customer at %s served ticket in row %d and column %d\n",
								current_time,
								all_names[args->seller_id].c_str(), row,
								column);
						res = pthread_mutex_unlock(&print_mutex);
						if (res != 0) {
							perror("Failed to unlock print mutex");
							printf("Failed to unlock print mutex");
							exit(EXIT_FAILURE);
						}
					} else {
						iterateNextSeat(&row, &column, args->seller_id);
					}

				}
				res = pthread_mutex_unlock(&seat_mutex);
				if (res != 0) {
					perror("Failed to unlock seat mutex");
					printf("Failed to unlock seat mutex");
					exit(EXIT_FAILURE);
				}
			}
		}

		//signal to main time thread that it is okay to proceed
		int res = sem_post(&(enter_time[args->seller_id]));
		if (res != 0) {
			perror("Semaphore failed to post");
			printf("Semaphore failed to post");
			exit(EXIT_FAILURE);
		}

	}

	//wait for all customer threads belonging to seller to finish before closing
	for (int i = 0; i < args->buyer_number; i++) {
		pthread_join(customer_threads[args->seller_id][i], NULL);
	}
	return NULL;
}

//function for all customer threads
void* customer_function(void *arg) {
	struct customer_arg* args = (customer_arg*) arg;
	srand(args->seed);
	int start_time = rand() % RUNTIME;
	bool isAddedToQueue = false;

	//run for 60 simulated minutes
	while (current_time < RUNTIME) {
		//wait for signal from main timer thread
		int res =
				sem_wait(
						&(new_semaphores[args->seller_id] + args->customer_id)->enter_customer);
		if (res != 0) {
			perror("Semaphore wait failed");
			printf("Semaphore wait failed");
			exit(EXIT_FAILURE);
		}

		//if arrival time reached, add customer to seller's queue
		if (isAddedToQueue == false && current_time == start_time) {
			int res = pthread_mutex_lock(&buyer_mutex[args->seller_id]);
			if (res != 0) {
				perror("buyer mutex lock failed");
				printf("buyter mutex lock failed");
				exit(EXIT_FAILURE);
			}
			isAddedToQueue = true;
			buyer_queue[args->seller_id].push_back(start_time);
			res = pthread_mutex_unlock(&buyer_mutex[args->seller_id]);
			if (res != 0) {
				perror("buyer mutex unlock failed");
				printf("buyter mutex unlock failed");
				exit(EXIT_FAILURE);
			}
			res = pthread_mutex_lock(&print_mutex);
			if (res != 0) {
				perror("print mutex lock failed");
				printf("print mutex lock failed");
				exit(EXIT_FAILURE);
			}
			printf("0:%02d; Added customer to seller %s's queue\n",
					current_time, all_names[args->seller_id].c_str());
			res = pthread_mutex_unlock(&print_mutex);
			if (res != 0) {
				perror("print mutex unlock failed");
				printf("print mutex unlock failed");
				exit(EXIT_FAILURE);
			}
		}

		//signal to seller thread that it may proceed
		res =
				sem_post(
						&(new_semaphores[args->seller_id] + args->customer_id)->enter_seller);
		if (res != 0) {
			perror("Semaphore post failed");
			printf("Semaphore post failed");
			exit(EXIT_FAILURE);
		}
	}

	return NULL;
}

