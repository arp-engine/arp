#include "network/tls/ssl_stream.h"

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace arp::network::tls {

namespace {

SslStream::IoResult makeResult(SslStream::IoAction action,
    TlsDiagnosticCode diagnostic = TlsDiagnosticCode::none,
    std::size_t bytes = 0)
{
    SslStream::IoResult result;
    result.action = action;
    result.status.diagnostic = diagnostic;
    result.bytes = bytes;
    return result;
}

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)

void drainOutput(SSL* ssl, byte_buffer& output)
{
    BIO* writeBio = SSL_get_wbio(ssl);
    if (writeBio == nullptr) {
        return;
    }
    std::uint8_t buffer[4096];
    for (;;) {
        const int bytes = BIO_read(writeBio, buffer, sizeof(buffer));
        if (bytes <= 0) {
            return;
        }
        output.insert(output.end(), buffer, buffer + bytes);
    }
}

SslStream::IoAction actionFromSslError(SSL* ssl, int result)
{
    const int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ) {
        return SslStream::IoAction::need_read;
    }
    if (error == SSL_ERROR_WANT_WRITE) {
        return SslStream::IoAction::need_write;
    }
    if (error == SSL_ERROR_ZERO_RETURN) {
        return SslStream::IoAction::failed;
    }
    return SslStream::IoAction::failed;
}

TlsDiagnosticCode diagnosticFromSslError(SSL* ssl, int result)
{
    const int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_ZERO_RETURN) {
        return TlsDiagnosticCode::connection_closed;
    }
    return TlsDiagnosticCode::handshake_failed;
}

#endif

} // namespace

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)

class SslStream::Impl final {
public:
    ~Impl()
    {
        if (ssl != nullptr) {
            SSL_free(ssl);
        }
    }

    SSL* ssl = nullptr;
};

#else

class SslStream::Impl final {};

#endif

SslStream::SslStream(TlsContext& context) : context_(&context), status_(context.status())
{}

SslStream::~SslStream() = default;

TlsStreamState SslStream::state() const noexcept
{
    return state_;
}

const TlsStatus& SslStream::status() const noexcept
{
    return status_;
}

TlsStatus SslStream::disabledStatus() const noexcept
{
    if (context_ == nullptr || !context_->status().success()) {
        return context_ == nullptr ? TlsStatus{TlsDiagnosticCode::disabled} : context_->status();
    }
    return TlsStatus{TlsDiagnosticCode::unsupported_backend};
}

TlsStatus SslStream::transportStatus() noexcept
{
    status_.diagnostic = TlsDiagnosticCode::transport_error;
    return status_;
}

TlsStatus SslStream::consume_ciphertext(const std::uint8_t* data, std::size_t size)
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    if (!impl_ || impl_->ssl == nullptr) {
        return TlsStatus{TlsDiagnosticCode::invalid_state};
    }
    BIO* readBio = SSL_get_rbio(impl_->ssl);
    if (readBio == nullptr
        || BIO_write(readBio, data, static_cast<int>(size)) != static_cast<int>(size)) {
        status_.diagnostic = TlsDiagnosticCode::transport_error;
        state_ = TlsStreamState::failed;
        return status_;
    }
    status_.diagnostic = TlsDiagnosticCode::none;
    return status_;
#else
    (void)data;
    (void)size;
    return disabledStatus();
#endif
}

SslStream::IoResult SslStream::handshake_step()
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    if (context_ == nullptr || !context_->status().success()) {
        status_ = disabledStatus();
        state_ = TlsStreamState::failed;
        return makeResult(IoAction::failed, status_.diagnostic);
    }
    if (!impl_) {
        SSL_CTX* ctx = static_cast<SSL_CTX*>(context_->native_handle());
        if (ctx == nullptr) {
            status_.diagnostic = TlsDiagnosticCode::unsupported_backend;
            state_ = TlsStreamState::failed;
            return makeResult(IoAction::failed, status_.diagnostic);
        }
        impl_ = core::makeUnique<Impl>();
        impl_->ssl = SSL_new(ctx);
        if (impl_->ssl == nullptr) {
            status_.diagnostic = TlsDiagnosticCode::backend_error;
            state_ = TlsStreamState::failed;
            return makeResult(IoAction::failed, status_.diagnostic);
        }
        BIO* readBio = BIO_new(BIO_s_mem());
        BIO* writeBio = BIO_new(BIO_s_mem());
        if (readBio == nullptr || writeBio == nullptr) {
            if (readBio != nullptr) {
                BIO_free(readBio);
            }
            if (writeBio != nullptr) {
                BIO_free(writeBio);
            }
            status_.diagnostic = TlsDiagnosticCode::backend_error;
            state_ = TlsStreamState::failed;
            return makeResult(IoAction::failed, status_.diagnostic);
        }
        BIO_set_mem_eof_return(readBio, -1);
        BIO_set_mem_eof_return(writeBio, -1);
        SSL_set_bio(impl_->ssl, readBio, writeBio);
        if (context_->config().endpoint_role() == TlsEndpointRole::client) {
            SSL_set_connect_state(impl_->ssl);
            if (!context_->config().server_name().empty()) {
                SSL_set_tlsext_host_name(impl_->ssl, context_->config().server_name().data());
            }
        } else {
            SSL_set_accept_state(impl_->ssl);
        }
    }

    state_ = TlsStreamState::handshaking;
    ERR_clear_error();
    const int result = SSL_do_handshake(impl_->ssl);
    auto io = makeResult(IoAction::done);
    drainOutput(impl_->ssl, io.output);
    if (result == 1) {
        status_.diagnostic = TlsDiagnosticCode::none;
        io.status = status_;
        state_ = TlsStreamState::established;
        return io;
    }
    io.action = actionFromSslError(impl_->ssl, result);
    if (io.action == IoAction::failed) {
        status_.diagnostic = diagnosticFromSslError(impl_->ssl, result);
        io.status = status_;
        state_ = TlsStreamState::failed;
    }
    return io;
#else
    status_ = disabledStatus();
    state_ = TlsStreamState::failed;
    return makeResult(IoAction::failed, status_.diagnostic);
#endif
}

SslStream::IoResult SslStream::recv_step(byte_buffer& buffer)
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    if (!impl_ || impl_->ssl == nullptr || state_ != TlsStreamState::established) {
        status_.diagnostic = TlsDiagnosticCode::invalid_state;
        return makeResult(IoAction::failed, status_.diagnostic);
    }
    ERR_clear_error();
    const int result = SSL_read(impl_->ssl, buffer.data(), static_cast<int>(buffer.size()));
    auto io = makeResult(IoAction::done);
    drainOutput(impl_->ssl, io.output);
    if (result > 0) {
        status_.diagnostic = TlsDiagnosticCode::none;
        io.status = status_;
        io.bytes = static_cast<std::size_t>(result);
        return io;
    }
    io.action = actionFromSslError(impl_->ssl, result);
    if (io.action == IoAction::failed) {
        status_.diagnostic = diagnosticFromSslError(impl_->ssl, result);
        io.status = status_;
        state_ = TlsStreamState::failed;
    }
    return io;
#else
    (void)buffer;
    status_ = disabledStatus();
    state_ = TlsStreamState::failed;
    return makeResult(IoAction::failed, status_.diagnostic);
#endif
}

SslStream::IoResult SslStream::send_step(const void* data, std::size_t size)
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    if (!impl_ || impl_->ssl == nullptr || state_ != TlsStreamState::established) {
        status_.diagnostic = TlsDiagnosticCode::invalid_state;
        return makeResult(IoAction::failed, status_.diagnostic);
    }
    ERR_clear_error();
    const int result = SSL_write(impl_->ssl, data, static_cast<int>(size));
    auto io = makeResult(IoAction::done);
    drainOutput(impl_->ssl, io.output);
    if (result > 0) {
        status_.diagnostic = TlsDiagnosticCode::none;
        io.status = status_;
        io.bytes = static_cast<std::size_t>(result);
        return io;
    }
    io.action = actionFromSslError(impl_->ssl, result);
    if (io.action == IoAction::failed) {
        status_.diagnostic = diagnosticFromSslError(impl_->ssl, result);
        io.status = status_;
        state_ = TlsStreamState::failed;
    }
    return io;
#else
    (void)data;
    (void)size;
    status_ = disabledStatus();
    state_ = TlsStreamState::failed;
    return makeResult(IoAction::failed, status_.diagnostic);
#endif
}

SslStream::IoResult SslStream::shutdown_step()
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    if (!impl_ || impl_->ssl == nullptr) {
        status_.diagnostic = TlsDiagnosticCode::invalid_state;
        state_ = TlsStreamState::shutdown;
        return makeResult(IoAction::failed, status_.diagnostic);
    }
    ERR_clear_error();
    const int result = SSL_shutdown(impl_->ssl);
    auto io = makeResult(result == 1 ? IoAction::done : IoAction::need_read);
    drainOutput(impl_->ssl, io.output);
    if (result == 1) {
        status_.diagnostic = TlsDiagnosticCode::none;
        io.status = status_;
        state_ = TlsStreamState::shutdown;
        return io;
    }
    if (result < 0) {
        io.action = actionFromSslError(impl_->ssl, result);
        if (io.action == IoAction::failed) {
            status_.diagnostic = diagnosticFromSslError(impl_->ssl, result);
            io.status = status_;
            state_ = TlsStreamState::shutdown;
        }
    }
    return io;
#else
    status_ = disabledStatus();
    state_ = TlsStreamState::shutdown;
    return makeResult(IoAction::failed, status_.diagnostic);
#endif
}

} // namespace arp::network::tls
