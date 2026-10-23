#pragma once
#include <memory>
#include <mutex>
#include <utility>

template<class T> struct SspSourceLifetime {
	std::recursive_mutex mutex;
	T *source = nullptr;
};
template<class T, class Callback> auto ssp_with_source(std::shared_ptr<SspSourceLifetime<T>> life, Callback callback)
{
	return [life, callback = std::move(callback)](auto &&...arguments) mutable {
		std::lock_guard<std::recursive_mutex> lock(life->mutex);
		if (life->source)
			callback(life->source, std::forward<decltype(arguments)>(arguments)...);
	};
}
