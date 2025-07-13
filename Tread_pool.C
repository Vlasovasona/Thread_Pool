#include <iostream>
#include <random>
#include <chrono>
#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cmath>

const size_t MAX_THREADS = 30; // максимальное кол-во потоков
const size_t ARRAY_SIZE = 100000000; // размер массива (по условия задачи 100 000 000)
const int MIN_VAL = 10000; // левая граница интервала генерации чисел
const int MAX_VAL = 100000; // правая граница интервала генерации чисел


class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    void enqueue(std::function<void()> task);
    void wait_for_completion();

private:
    struct Node {
        std::function<void()> task;
        Node* next;
        Node(std::function<void()> t) : task(t), next(nullptr) {}
    };

    Node* head;
    Node* tail;

    std::mutex queue_mutex;
    std::condition_variable task_wait_new;

    bool stop_flag;
    size_t active_tasks;
    std::mutex state_mutex;
    std::condition_variable done;

    std::thread* threads;
    size_t thread_count;

    void worker_thread();
};

// Конструктор
ThreadPool::ThreadPool(size_t num_threads)
    : head(nullptr), tail(nullptr), stop_flag(false), active_tasks(0), thread_count(num_threads)
{
    threads = new std::thread[thread_count];
    for (size_t i = 0; i < thread_count; ++i) {
        threads[i] = std::thread(&ThreadPool::worker_thread, this);
    }
}

// Деструктор
ThreadPool::~ThreadPool() {
    wait_for_completion();
    delete[] threads;
}

// Добавление задач
void ThreadPool::enqueue(std::function<void()> task) {
    Node* new_node = new Node(task);
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!tail) {
            head = tail = new_node;
        }
        else {
            tail->next = new_node;
            tail = new_node;
        }
        {
            std::lock_guard<std::mutex> lock2(state_mutex);
            ++active_tasks;
        }
    }
    task_wait_new.notify_one();
}

// Ожидание завершения
void ThreadPool::wait_for_completion() {
    // Ждем, пока активных задач не станет 0
    std::unique_lock<std::mutex> lock(state_mutex);
    done.wait(lock, [this]() { return active_tasks == 0; });

    // Устанавливаем флаг для завершения потоков
    {
        std::lock_guard<std::mutex> lock2(queue_mutex);
        stop_flag = true;
    }
    task_wait_new.notify_all();

    for (size_t i = 0; i < thread_count; ++i) {
        if (threads[i].joinable()) {
            threads[i].join();
        }
    }
}

// Обработка задач
void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            task_wait_new.wait(lock, [this]() { return head != nullptr || stop_flag; });

            if (stop_flag && head == nullptr) {
                return;
            }

            Node* node = head;
            head = head->next;
            if (head == nullptr) {
                tail = nullptr;
            }
            task = node->task;
            delete node;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            --active_tasks;
            if (active_tasks == 0) {
                done.notify_all();
            }
        }
    }
}

// Генерация массива случайных чисел
int* generate_random_array(size_t size) {
    int* arr = new int[size]; // создание динамического массива размера size

    // Инициализация генератора случайных чисел один раз
    static bool initialized = false;
    if (!initialized) {
        std::srand(std::time(nullptr));
        initialized = true;
    }

    for (size_t i = 0; i < size; ++i) {
        int rand_num = std::rand(); // генерируем случайное число
        arr[i] = MIN_VAL + rand_num % (MAX_VAL - MIN_VAL + 1);
    }
    return arr;
}

// Проверка, является ли число простым
bool is_prime(int n) {
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;
    int sqrt_n = static_cast<int>(std::sqrt(n));
    for (int i = 3; i <= sqrt_n; i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

// функция для подсчета простых чисел при использовании одного потока
size_t count_primes_single_thread(const int* arr) {
    unsigned int count = 0;
    auto start_time = std::chrono::steady_clock::now(); // стартовое время работы функции

    for (size_t i = 0; i < ARRAY_SIZE; ++i) { //подсчет простых чисел в массиве arr
        if (is_prime(arr[i])) {
            count += 1;
        }
    }

    auto end_time = std::chrono::steady_clock::now(); // время завершения обработки
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count(); // вычисление длительности обработки в мс

    std::cout << "One thread: the amount of numbers = " << count //вывод результата
        << ", time = " << duration_ms << " ms" << std::endl;

    return count; //кол-во найденных простых чисел
}


size_t count_primes_multi_thread(const int* arr, size_t thread_count) {

    // общая переменная для подсчёта простых чисел
    unsigned int total_count = 0;

    // мьютекс для защиты общего счётчика от одновременного изменения несколькими потоками
    std::mutex mtx;

    // определяем размер блока данных для каждого потока
    size_t block_size = ARRAY_SIZE / thread_count;

    // стартовая точка измерения времени
    auto start_time = std::chrono::steady_clock::now();

    // создаю  массив потоков
    std::thread* threads = new std::thread[thread_count];

    // запускаем потоки
    for (size_t t = 0; t < thread_count; ++t) {
        size_t start_idx = t * block_size;   // начало блока для данного потока
        size_t end_idx = (t == thread_count - 1) ? ARRAY_SIZE : start_idx + block_size; // конец блока

        threads[t] = std::thread([&arr, &total_count, &mtx, start_idx, end_idx]() mutable {
            unsigned int local_count = 0;

            // считаем количество простых чисел в своём диапазоне
            for (size_t i = start_idx; i < end_idx; ++i) {
                if (is_prime(arr[i])) {
                    local_count += 1;
                }
            }

            // защищённая секция обновления общего счётчика
            std::lock_guard<std::mutex> lock(mtx);
            total_count += local_count;
            });
    }

    // ожидаем завершение всех потоков
    for (size_t t = 0; t < thread_count; ++t) {
        if (threads[t].joinable()) {
            threads[t].join(); // блокирует вызывающтй поток до тех пор, пока не завершится поток-объект
        }
    }

    // измерение времени окончания
    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "Multithreaded mode (" << thread_count << " threads): number of prime numbers = " << total_count
               << ", time = " << duration_ms << " ms" << std::endl;

    delete[] threads; // освобождение памяти

    return total_count;
}

 //Функция подсчета простых чисел с использованием пула потоков
size_t count_primes_with_threadpool(const int* arr, size_t pool_size) {
    // определение числа задач равным размеру пула потоков
    size_t kolvo_tasks = pool_size;
    // вычисление размера блока элементов, которые будет обрабатывать каждая задача
    size_t block_size = ARRAY_SIZE / pool_size;

    //size_t total_count = 0; // подстчет общего числа простых чисел массива
    unsigned int total_count = 0;
    std::mutex mutex; // мьютекс для защиты общего счетчика

    auto start_time = std::chrono::steady_clock::now(); // запуск времени

    // пул потоков с заданным размером
    ThreadPool pool(pool_size);

    // для каждой задачи (потока) создаем блок обработки
    for (size_t t = 0; t < kolvo_tasks; ++t) {
        size_t start = t * block_size;
        size_t end = (t == kolvo_tasks - 1) ? ARRAY_SIZE : start + block_size;

        pool.enqueue([&, start, end]() {
            unsigned int local_count = 0;
            for (size_t i = start; i < end; ++i) {
                if (is_prime(arr[i])) {
                    local_count += 1;
                }
            }
            // блокировка для обновления общего счетчика
            std::lock_guard<std::mutex> lock(mutex);
            total_count += local_count;
            });
    }

    pool.wait_for_completion(); // ждем завершения всех задач

    auto end_time = std::chrono::steady_clock::now(); // время окончания
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "Thread pool (" << pool_size << " threads): Number of prime numbers = " << total_count
    << ", time = " << duration_ms << " ms" << std::endl;

    return total_count;
}

int main() {
    unsigned int cores = std::thread::hardware_concurrency();
    std::cout << "Logical processors: " << cores << std::endl;

    std::cout << "Generate an array of " << ARRAY_SIZE << " random numbers..." << std::endl;
    int* arr = generate_random_array(ARRAY_SIZE);
    
    std::cout << "Generation completed." << std::endl;

    // Однопоточный режим

    for (int i = 0; i < 10; i++) {
        count_primes_single_thread(arr);
    }

    // Многопоточный режим
    //count_primes_multi_thread(arr, cores);

    // Использование пула потоков
    /*count_primes_with_threadpool(arr, 16);*/

    delete[] arr;
    return 0;
}